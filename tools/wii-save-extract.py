#!/usr/bin/env python3
"""Unpack a Wii save exported as data.bin into a NAND folder tree.

Two containers show up in the wild:

  official   System Menu export. A 0xF0C0-byte header encrypted with the
             shared SD key and SD IV, a 0x80-byte Bk header (0x426B0001 at
             +4), then one record per file: a 0x80-byte header starting with
             0x03ADF17E and holding its own AES-CBC IV at +0x50, followed by
             the payload (0x40-padded), then certificates.

  packed     Used by the save-sharing sites. Same SD key and IV over the
             whole file, but the payload is a 0x20-byte header, the banner,
             then the raw file contents back to back, then a certificate
             area. File names are not stored, so the destination's existing
             file list supplies the layout.

Neither format is bound to a console: the SD key is shared and only the
signature is per-console, which a recomp does not check.

Usage:
    python tools/wii-save-extract.py <data.bin> <out-dir> [--layout <dir>] [--list]
                                     [--sd-key <hex> --sd-iv <hex>]
    python tools/wii-save-extract.py --self-test

The SD key and IV are not included. Supply them (from your own console) with
--sd-key/--sd-iv, the WII_SD_KEY/WII_SD_IV environment variables, or
config/sd-keys.txt ("sd_key=<32 hex>" and "sd_iv=<32 hex>", one per line).

For a packed container, --layout must point at a folder that already has the
same file names and sizes (the game creates them on first boot). For the
official container it is optional and only used to name files.
"""

import os
import struct
import sys

from Crypto.Cipher import AES

# The shared Wii SD key and IV are not shipped with this project. They are
# loaded at run time by load_keys() from user-supplied data.
SD_KEY = None
SD_IV = None
KEYS_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), os.pardir, "config", "sd-keys.txt")

HEADER_SIZE = 0xF0C0
FILE_MAGIC = 0x03ADF17E
BLOCK = 0x40
BANNER_MAGIC = b"WIBN"
BK_SIZE = 0x80
BANNER_SIZE_TEST = 0x600  # synthetic banner used by --self-test
BANNER_BODY_TEST = bytes((i * 13 + 7) & 0xFF for i in range(BANNER_SIZE_TEST - 4))
BK_MAGIC = 0x426B0001
FILE_HEADER_SIZE = 0x80


def _parse_key(label, text):
    try:
        value = bytes.fromhex(text.strip())
    except ValueError:
        raise SystemExit("%s is not valid hex" % label)
    if len(value) != 16:
        raise SystemExit("%s must be 16 bytes (32 hex characters), got %d bytes" % (label, len(value)))
    return value


def load_keys(arg_key=None, arg_iv=None):
    """Set SD_KEY / SD_IV from --sd-key/--sd-iv, then WII_SD_KEY/WII_SD_IV, then config/sd-keys.txt."""
    global SD_KEY, SD_IV
    key = arg_key or os.environ.get("WII_SD_KEY")
    iv = arg_iv or os.environ.get("WII_SD_IV")
    if (not key or not iv) and os.path.isfile(KEYS_FILE):
        with open(KEYS_FILE, encoding="utf-8") as handle:
            for line in handle:
                name, sep, text = line.partition("=")
                if not sep or line.lstrip().startswith("#"):
                    continue
                name = name.strip().lower()
                if name == "sd_key" and not key:
                    key = text
                elif name == "sd_iv" and not iv:
                    iv = text
    if not key or not iv:
        raise SystemExit(
            "SD key and SD IV are required and are not included with this project.\n"
            "Supply them from your own console (for example dumped with BootMii or a keys.bin tool) by any of:\n"
            "  --sd-key <32 hex> --sd-iv <32 hex>\n"
            "  environment variables WII_SD_KEY and WII_SD_IV\n"
            "  a file %s with two lines: sd_key=<32 hex> and sd_iv=<32 hex>" % os.path.normpath(KEYS_FILE)
        )
    SD_KEY = _parse_key("SD key", key)
    SD_IV = _parse_key("SD IV", iv)


def decrypt(key, iv, data):
    if len(data) % 16:
        data = data[: len(data) - (len(data) % 16)]
    return AES.new(key, AES.MODE_CBC, iv).decrypt(data)


def safe_name(raw):
    name = raw.split(b"\x00", 1)[0].decode("utf-8", "surrogateescape")
    name = name.replace("\\", "/").lstrip("/")
    parts = [p for p in name.split("/") if p not in ("", ".", "..")]
    if not parts:
        raise ValueError("empty file name")
    return os.path.join(*parts)


def list_layout(directory):
    """Return (name, size) pairs in the order the console stores them.

    banner.bin comes first because the packed container always starts with
    it; the rest are sorted so RFF2_02 precedes RFF2_10.
    """
    entries = []
    for name in sorted(os.listdir(directory)):
        path = os.path.join(directory, name)
        if os.path.isfile(path):
            entries.append((name, os.path.getsize(path)))
    entries.sort(key=lambda item: (item[0] != "banner.bin", item[0]))
    return entries


def unpack_official(raw, out_dir, list_only):
    """Unpack the official container.

    Layout (all offsets from the start of data.bin):
      0x0000  0xF0C0 bytes  data.bin header + banner, AES-CBC with SD key/IV;
                            decrypted: banner size at +8, WIBN banner at +0x20
                            (written out as banner.bin)
      0xF0C0  0x80 bytes    Bk header: size 0x70 at +0, 0x426B0001 at +4,
                            file count at +0x0C, total size at +0x1C,
                            title ID at +0x60, console MAC at +0x68
      0xF140  per file      0x80-byte file header (0x03ADF17E at +0, size at
                            +4, type at +0x0A, name at +0x0B, AES IV at
                            +0x50) followed by the payload, AES-CBC with the
                            SD key and that IV, padded to a multiple of 0x40
      after   certificates (ignored)

    The payload starts at header + 0x80. Reading it at the header itself
    shifts every extracted file 0x80 bytes early.
    """
    bk = HEADER_SIZE
    if len(raw) < bk + BK_SIZE:
        raise SystemExit("file is too short for a Bk header at 0x%X" % bk)

    bk_size, bk_magic = struct.unpack_from(">II", raw, bk)
    if bk_magic != BK_MAGIC:
        raise SystemExit(
            "no Bk header at 0x%X: expected 0x%08X at +4, found 0x%08X"
            % (bk, BK_MAGIC, bk_magic)
        )
    if bk_size != 0x70:
        print("warning: Bk header size field is 0x%X, expected 0x70" % bk_size)

    ng_id, files = struct.unpack_from(">II", raw, bk + 0x08)
    total_size = struct.unpack_from(">I", raw, bk + 0x1C)[0]
    game_id = raw[bk + 0x64 : bk + 0x68].decode("ascii", "replace")
    mac = ":".join("%02x" % b for b in raw[bk + 0x68 : bk + 0x6E])

    print(
        "official container  game %s  files %d  total %d bytes  console %s"
        % (game_id, files, total_size, mac)
    )

    head = decrypt(SD_KEY, SD_IV, raw[:HEADER_SIZE])
    banner_size = struct.unpack_from(">I", head, 0x08)[0]
    banner = head[0x20 : 0x20 + banner_size]
    if banner[:4] != BANNER_MAGIC or len(banner) != banner_size:
        raise SystemExit(
            "no valid banner in the decrypted header (size 0x%X, magic %r)"
            % (banner_size, banner[:4])
        )
    print("  %-32s %-9s %8d" % ("banner.bin", "file", banner_size))
    written = 0
    if not list_only:
        os.makedirs(out_dir, exist_ok=True)
        with open(os.path.join(out_dir, "banner.bin"), "wb") as handle:
            handle.write(banner)
        written += 1

    offset = bk + BK_SIZE
    for index in range(files):
        if offset + FILE_HEADER_SIZE > len(raw):
            raise SystemExit("truncated at file %d (header at 0x%X)" % (index, offset))

        file_magic, file_size = struct.unpack_from(">II", raw, offset)
        if file_magic != FILE_MAGIC:
            raise SystemExit(
                "bad file magic 0x%08X at 0x%X (file %d of %d), expected 0x%08X"
                % (file_magic, offset, index, files, FILE_MAGIC)
            )

        kind = raw[offset + 0x0A]
        name = safe_name(raw[offset + 0x0B : offset + 0x50])
        iv = raw[offset + 0x50 : offset + 0x60]
        start = offset + FILE_HEADER_SIZE
        padded = (file_size + BLOCK - 1) // BLOCK * BLOCK
        is_dir = kind == 2
        if not is_dir and start + padded > len(raw):
            raise SystemExit(
                "truncated payload for %s: need 0x%X bytes at 0x%X" % (name, padded, start)
            )

        print("  %-32s %-9s %8d" % (name, "directory" if is_dir else "file", file_size))

        if not list_only:
            target = os.path.join(out_dir, name)
            if is_dir:
                os.makedirs(target, exist_ok=True)
            else:
                os.makedirs(os.path.dirname(target) or ".", exist_ok=True)
                plain = decrypt(SD_KEY, iv, raw[start : start + padded]) if file_size else b""
                with open(target, "wb") as handle:
                    handle.write(plain[:file_size])
                written += 1

        offset = start + (0 if is_dir else padded)

    if not list_only:
        print("wrote %d files into %s" % (written, out_dir))


def unpack_packed(plain, out_dir, layout_dir, list_only):
    title_hi, game_id = struct.unpack_from(">I4s", plain, 0)
    banner_size = struct.unpack_from(">I", plain, 0x08)[0]
    if title_hi != 0x00010000 or plain[0x20:0x24] != BANNER_MAGIC:
        raise SystemExit("packed header check failed")

    print(
        "packed container  game %s  banner %d bytes  payload starts at 0x%X"
        % (game_id.decode("ascii", "replace"), banner_size, HEADER_SIZE)
    )

    if not layout_dir:
        raise SystemExit(
            "packed containers do not store file names; pass --layout with a\n"
            "folder that already holds this save's files (the game creates\n"
            "them on first boot)"
        )

    layout = list_layout(layout_dir)
    if not layout or layout[0][0] != "banner.bin":
        raise SystemExit("layout has no banner.bin: %s" % layout_dir)

    expected = sum(size for name, size in layout if name != "banner.bin")
    available = len(plain) - HEADER_SIZE
    if available < expected:
        raise SystemExit(
            "payload is %d bytes but the layout needs %d" % (available, expected)
        )

    offset = HEADER_SIZE
    written = 0
    for name, size in layout:
        if name == "banner.bin":
            source = plain[0x20 : 0x20 + banner_size]
            if len(source) != size:
                raise SystemExit(
                    "banner is %d bytes in the container but %d in the layout"
                    % (len(source), size)
                )
        else:
            source = plain[offset : offset + size]
            offset += size

        print("  %-32s file      %8d" % (name, size))
        if not list_only:
            target = os.path.join(out_dir, name)
            os.makedirs(os.path.dirname(target) or ".", exist_ok=True)
            with open(target, "wb") as handle:
                handle.write(source)
            written += 1

    tail = plain[offset:]
    if any(tail):
        print("note: %d trailing bytes skipped (certificate area)" % len(tail))
    if not list_only:
        print("wrote %d files into %s" % (written, out_dir))


def unpack(path, out_dir, layout_dir, list_only):
    raw = open(path, "rb").read()
    if len(raw) < HEADER_SIZE:
        raise SystemExit("%s is shorter than a Wii header" % path)

    if len(raw) >= HEADER_SIZE + 8:
        if struct.unpack_from(">I", raw, HEADER_SIZE + 4)[0] == BK_MAGIC:
            unpack_official(raw, out_dir, list_only)
            return

    # No Bk header after the banner: the whole file is one CBC stream.
    plain = decrypt(SD_KEY, SD_IV, raw)
    unpack_packed(plain, out_dir, layout_dir, list_only)


def build_synthetic(files):
    """Build an official-layout data.bin in memory from {name: plaintext}."""
    head = bytearray(HEADER_SIZE)
    struct.pack_into(">I4s", head, 0, 0x00010000, b"SO3E")
    struct.pack_into(">I", head, 0x08, BANNER_SIZE_TEST)
    head[0x20:0x24] = BANNER_MAGIC
    head[0x24 : 0x20 + BANNER_SIZE_TEST] = BANNER_BODY_TEST
    out = bytearray(AES.new(SD_KEY, AES.MODE_CBC, SD_IV).encrypt(bytes(head)))

    bk = bytearray(BK_SIZE)
    struct.pack_into(">IIII", bk, 0, 0x70, BK_MAGIC, 0x12345678, len(files))
    bk[0x60:0x68] = struct.pack(">I4s", 0x00010000, b"SO3E")
    bk[0x68:0x6E] = bytes.fromhex("0017ab123456")
    out += bk

    for number, (name, plain) in enumerate(files.items()):
        iv = bytes((number * 16 + i * 7 + 1) & 0xFF for i in range(16))
        padded = (len(plain) + BLOCK - 1) // BLOCK * BLOCK
        fh = bytearray(FILE_HEADER_SIZE)
        struct.pack_into(">II", fh, 0, FILE_MAGIC, len(plain))
        fh[0x08:0x0B] = bytes([0x35, 0x00, 0x01])
        fh[0x0B : 0x0B + len(name)] = name.encode("ascii")
        fh[0x50:0x60] = iv
        body = plain + b"\x00" * (padded - len(plain))
        out += fh + AES.new(SD_KEY, AES.MODE_CBC, iv).encrypt(body)

    out += b"\xAA" * 0x40  # stand-in for the certificate area
    return bytes(out)


def self_test():
    import contextlib
    import io
    import tempfile

    files = {
        "RFF2_00": bytes((i * 31 + 5) & 0xFF for i in range(0x1234)),  # not 0x40-aligned
        "RFF2_01": bytes((i * 17 + 3) & 0xFF for i in range(0x80)),
    }
    global SD_KEY, SD_IV
    SD_KEY = os.urandom(16)  # throwaway keys: the self-test needs no real key
    SD_IV = os.urandom(16)
    blob = build_synthetic(files)

    with tempfile.TemporaryDirectory() as tmp:
        source = os.path.join(tmp, "data.bin")
        with open(source, "wb") as handle:
            handle.write(blob)

        out_dir = os.path.join(tmp, "out")
        os.makedirs(out_dir)
        unpack(source, out_dir, None, False)
        expected_banner = BANNER_MAGIC + BANNER_BODY_TEST
        files_check = dict(files)
        files_check["banner.bin"] = expected_banner
        for name, plain in files_check.items():
            with open(os.path.join(out_dir, name), "rb") as handle:
                got = handle.read()
            if got != plain:
                raise SystemExit("self-test FAILED: %s differs (%d vs %d bytes)" % (name, len(got), len(plain)))
            print("self-test ok: %s %d bytes match" % (name, len(plain)))

        def expect_failure(label, data):
            bad = os.path.join(tmp, "bad.bin")
            with open(bad, "wb") as handle:
                handle.write(data)
            sink = io.StringIO()
            try:
                with contextlib.redirect_stdout(sink):
                    unpack(bad, os.path.join(tmp, "bad-out"), None, False)
            except SystemExit as error:
                print("self-test ok: %s rejected (%s)" % (label, str(error).split(",")[0]))
                return
            raise SystemExit("self-test FAILED: %s was accepted" % label)

        second = HEADER_SIZE + BK_SIZE + FILE_HEADER_SIZE + 0x1240  # header of record 1
        broken = bytearray(blob)
        broken[second : second + 4] = b"\x00\x00\x00\x00"
        expect_failure("missing file magic", bytes(broken))

        print("self-test passed")


def main(argv):
    positional = []
    layout_dir = None
    list_only = False
    arg_key = None
    arg_iv = None

    index = 1
    while index < len(argv):
        value = argv[index]
        if value == "--layout":
            index += 1
            if index >= len(argv):
                raise SystemExit("--layout needs a folder")
            layout_dir = argv[index]
        elif value in ("--sd-key", "--sd-iv"):
            index += 1
            if index >= len(argv):
                raise SystemExit("%s needs 32 hex characters" % value)
            if value == "--sd-key":
                arg_key = argv[index]
            else:
                arg_iv = argv[index]
        elif value == "--list":
            list_only = True
        elif value == "--self-test":
            self_test()
            return
        elif value.startswith("--"):
            raise SystemExit("unknown option: %s" % value)
        else:
            positional.append(value)
        index += 1

    if len(positional) != 2:
        raise SystemExit(__doc__)

    path, out_dir = positional
    load_keys(arg_key, arg_iv)
    if not os.path.isfile(path):
        raise SystemExit("no such file: %s" % path)
    os.makedirs(out_dir, exist_ok=True)

    unpack(path, out_dir, layout_dir, list_only)


if __name__ == "__main__":
    main(sys.argv)
