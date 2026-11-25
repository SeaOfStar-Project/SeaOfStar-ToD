#!/usr/bin/env python3
"""Summarise the Rune Factory: Tides of Destiny save files (RFF2_00..RFF2_15).

Stdlib only. All numeric columns are RAW big-endian values read from fixed
offsets; what they mean (money, playtime, story flag...) is NOT confirmed.

Layout used (observed, not specified):
  RFF2_00       slot index. From 0x10, 0xB0-byte entries; entry k (0-based)
                describes file RFF2_(k+1). Name = UTF-16BE at the entry start.
                The 0x28 bytes before the name are an unknown metadata block.
  RFF2_01..15   starts with the header (first 0x20 bytes look like a digest);
                offsets are from the file start, correctly aligned extracts only
                (older shifted extracts had every offset 0x80 too high).

  save-info.py [DATA_DIR] [--json]
"""
import argparse
import hashlib
import json
import os
import struct
import sys

DEFAULT_DIR = os.path.normpath(os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..",
    "work", "wii-recomp", "recomp", "build", "saves", "title",
    "00010000", "534f3345", "data"))

SLOT_SIZE = 47936
HEADER_SIZE = 0
INDEX_START = 0x10
ENTRY_SIZE = 0xB0
META_SIZE = 0x28


def be32(data, off):
    return struct.unpack_from(">I", data, off)[0]


def index_name(index, k):
    start = INDEX_START + k * ENTRY_SIZE
    if start + ENTRY_SIZE > len(index):
        return None, None
    raw = index[start:start + ENTRY_SIZE]
    end = len(raw) - len(raw) % 2
    chars = []
    for i in range(0, end, 2):
        unit = raw[i:i + 2]
        if unit == b"\x00\x00":
            break
        chars.append(unit)
    name = b"".join(chars).decode("utf-16-be", errors="replace")
    meta = index[max(0, start - META_SIZE):start].hex()
    return name, meta


def read(path):
    try:
        with open(path, "rb") as f:
            return f.read()
    except OSError:
        return None


def collect(folder):
    index = read(os.path.join(folder, "RFF2_00")) or b""
    rows = []
    for n in range(0, 16):
        fname = "RFF2_%02d" % n
        data = read(os.path.join(folder, fname))
        row = {"file": fname, "present": data is not None}
        if data is None:
            rows.append(row)
            continue
        row["size"] = len(data)
        row["sha256"] = hashlib.sha256(data).hexdigest()
        if n == 0:
            row["kind"] = "index"
            row["used"] = any(data)
        else:
            row["kind"] = "slot"
            body = data[HEADER_SIZE:]
            row["used"] = any(body)
            name, meta = index_name(index, n - 1)
            row["index_name"] = name
            row["index_meta_hex"] = meta
            if len(data) >= 0x10:
                row["raw_u32_0x84"] = be32(data, 0x04)
                row["raw_u32_0x8C"] = be32(data, 0x0C)
        rows.append(row)
    return rows


def main():
    for stream in (sys.stdout, sys.stderr):
        stream.reconfigure(encoding="utf-8", errors="replace")
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("folder", nargs="?", default=DEFAULT_DIR)
    ap.add_argument("--json", action="store_true", help="machine-readable output")
    args = ap.parse_args()
    if not os.path.isdir(args.folder):
        print("no such folder: %s" % args.folder, file=sys.stderr)
        return 1
    rows = collect(args.folder)
    if args.json:
        print(json.dumps({"folder": args.folder,
                          "note": "raw_* fields are raw big-endian values; meaning unconfirmed",
                          "files": rows}, indent=2))
        return 0
    print("folder: %s" % args.folder)
    print("raw_0x04 / raw_0x0C are RAW big-endian u32 values; their meaning is unconfirmed.")
    print()
    print("%-9s %-6s %-18s %12s %12s  %s" % (
        "file", "state", "index name", "raw_0x04", "raw_0x0C", "sha256[:12]"))
    for r in rows:
        if not r["present"]:
            print("%-9s %-6s" % (r["file"], "MISSING"))
            continue
        if r["kind"] == "index":
            print("%-9s %-6s %-18s %12s %12s  %s" % (
                r["file"], "index", "(slot index)", "-", "-", r["sha256"][:12]))
            continue
        print("%-9s %-6s %-18s %12s %12s  %s" % (
            r["file"], "used" if r["used"] else "empty",
            (r.get("index_name") or "")[:18],
            r.get("raw_u32_0x84", "-"),
            ("0x%08X" % r["raw_u32_0x8C"]) if "raw_u32_0x8C" in r else "-",
            r["sha256"][:12]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
