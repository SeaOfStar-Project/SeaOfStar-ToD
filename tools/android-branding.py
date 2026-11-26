#!/usr/bin/env python3
"""Android branding step: app icon from the user's own disc, built locally only.

Reads opening.bnr from the extracted disc tree (IMET -> U8 -> banner.bin / icon.bin, IMD5 + LZ77 wrapped,
TPL textures) and writes, under work/android-build/branding/ (gitignored):

  source/            decoded TPL textures as PNG (game-derived, local only)
  res/               an Android resource overlay: mipmap-*/ic_launcher.png, mipmap-anydpi-v26 adaptive icon,
                     values/app_name.xml (the phone label, overrides the neutral app_name)
  icon-preview.png   the final icon
  branding.json      what was used (source file, size, app label)

The gradle build takes -PbrandingDir=<that folder>; its res/ overlays the debug and release source sets. Nothing produced here is tracked:
without a dump the repository builds with its neutral default icon and the label SeaOfStar-ToD (ADR-008).

  python tools/android-branding.py [--disc extracted/SO3EE9] [--out work/android-build/branding] [--list]
"""
import argparse, json, os, struct, sys
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
LABEL = "Rune Factory - Tides of Destiny"


def lz10(b):
    assert b[:4] == b"LZ77" and b[4] == 0x10, "unexpected compression"
    size = b[5] | b[6] << 8 | b[7] << 16
    p, out = 8, bytearray()
    while len(out) < size:
        flags = b[p]; p += 1
        for bit in range(8):
            if len(out) >= size: break
            if flags & (0x80 >> bit):
                v = b[p] << 8 | b[p + 1]; p += 2
                n, disp = (v >> 12) + 3, (v & 0xFFF) + 1
                for _ in range(n): out.append(out[-disp])
            else:
                out.append(b[p]); p += 1
    return bytes(out[:size])


def unwrap(b):
    if b[:4] == b"IMD5": b = b[32:]
    if b[:4] == b"LZ77": b = lz10(b)
    return b


def u8_entries(b):
    assert b[:4] == b"U8\x00\x00"[:0] + b"\x55\xAA\x38\x2D"
    ro = struct.unpack(">I", b[4:8])[0]
    n = struct.unpack(">I", b[ro + 8:ro + 12])[0]
    names = ro + n * 12
    files, stack = {}, [("", n)]
    for k in range(1, n):
        while len(stack) > 1 and k >= stack[-1][1]: stack.pop()
        tn, off, sz = struct.unpack(">III", b[ro + k * 12:ro + k * 12 + 12])
        no = tn & 0xFFFFFF
        nm = b[names + no:b.index(b"\0", names + no)].decode()
        if tn >> 24: stack.append((stack[-1][0] + nm + "/", sz))
        else: files[stack[-1][0] + nm] = b[off:off + sz]
    return files


def rgb565(v): return ((v >> 11) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31, 255)
def rgb5a3(v):
    if v & 0x8000: return (((v >> 10) & 31) * 255 // 31, ((v >> 5) & 31) * 255 // 31, (v & 31) * 255 // 31, 255)
    return (((v >> 8) & 15) * 17, ((v >> 4) & 15) * 17, (v & 15) * 17, ((v >> 12) & 7) * 255 // 7)


def tiled(w, h, bw, bh, bytes_per_block_fn):
    """Yield (x, y) pixel coordinates in GX tile order for blocks of bw x bh."""
    for ty in range(0, h, bh):
        for tx in range(0, w, bw):
            for y in range(bh):
                for x in range(bw):
                    yield tx + x, ty + y


def decode_tpl(t):
    assert struct.unpack(">I", t[:4])[0] == 0x0020AF30
    cnt, tab = struct.unpack(">II", t[4:12])
    images = []
    for i in range(cnt):
        ioff, poff = struct.unpack(">II", t[tab + i * 8:tab + i * 8 + 8])
        h, w, fmt, doff = struct.unpack(">HHII", t[ioff:ioff + 12])
        pal = None
        if poff:
            pn, _, pfmt, pdoff = struct.unpack(">HHII", t[poff:poff + 12])
            pc = struct.unpack(">%dH" % pn, t[pdoff:pdoff + pn * 2])
            pal = [(rgb565 if pfmt == 1 else rgb5a3)(v) if pfmt != 0 else (v >> 8, v >> 8, v >> 8, v & 255) for v in pc]
        img = Image.new("RGBA", (w, h))
        px = img.load()
        d = t[doff:]
        def put(x, y, c):
            if x < w and y < h: px[x, y] = c
        p = 0
        if fmt == 0:   # I4
            for ty in range(0, h, 8):
                for tx in range(0, w, 8):
                    for y in range(8):
                        for x in range(0, 8, 2):
                            b = d[p]; p += 1
                            for k, n in enumerate((b >> 4, b & 15)):
                                v = n * 17; put(tx + x + k, ty + y, (v, v, v, v))
        elif fmt in (1, 2, 9, 3, 4, 5, 8):
            bw, bh, bpp = {1: (8, 4, 1), 2: (8, 4, 1), 9: (8, 4, 1), 3: (4, 4, 2), 4: (4, 4, 2), 5: (4, 4, 2), 8: (8, 8, 0.5)}[fmt]
            for ty in range(0, h, bh):
                for tx in range(0, w, bw):
                    for y in range(bh):
                        for x in range(bw):
                            if fmt == 1: v = d[p]; p += 1; c = (v, v, v, v)
                            elif fmt == 2: v = d[p]; p += 1; a, i = (v >> 4) * 17, (v & 15) * 17; c = (i, i, i, a)
                            elif fmt == 9: c = pal[d[p]]; p += 1
                            elif fmt == 3: i, a = d[p], d[p + 1]; p += 2; c = (i, i, i, a)
                            elif fmt == 4: c = rgb565(d[p] << 8 | d[p + 1]); p += 2
                            elif fmt == 5: c = rgb5a3(d[p] << 8 | d[p + 1]); p += 2
                            else:
                                v = d[p + (x >> 1) if False else p]; c = None
                                b = d[p]; 
                                idx = b >> 4 if x % 2 == 0 else b & 15
                                c = pal[idx]
                                if x % 2 == 1: p += 1
                            put(tx + x, ty + y, c)
        elif fmt == 6:  # RGBA32: 4x4 blocks, AR then GB
            for ty in range(0, h, 4):
                for tx in range(0, w, 4):
                    blk = d[p:p + 64]; p += 64
                    for k in range(16):
                        put(tx + k % 4, ty + k // 4, (blk[k * 2 + 1], blk[32 + k * 2], blk[32 + k * 2 + 1], blk[k * 2]))
        elif fmt == 14:  # CMPR
            for ty in range(0, h, 8):
                for tx in range(0, w, 8):
                    for sy in range(0, 8, 4):
                        for sx in range(0, 8, 4):
                            c0, c1 = struct.unpack(">HH", d[p:p + 4])
                            a, b = rgb565(c0), rgb565(c1)
                            if c0 > c1:
                                cols = [a, b, tuple((2 * a[k] + b[k]) // 3 for k in range(3)) + (255,), tuple((a[k] + 2 * b[k]) // 3 for k in range(3)) + (255,)]
                            else:
                                cols = [a, b, tuple((a[k] + b[k]) // 2 for k in range(3)) + (255,), (0, 0, 0, 0)]
                            for y in range(4):
                                row = d[p + 4 + y]
                                for x in range(4):
                                    put(tx + sx + x, ty + sy + y, cols[(row >> (6 - 2 * x)) & 3])
                            p += 8
        else:
            raise SystemExit("TPL format %d not supported" % fmt)
        images.append((fmt, img))
    return images


def sources(disc):
    bnr = Path(disc) / "files" / "opening.bnr"
    raw = bnr.read_bytes()
    j = raw.find(b"\x55\xAA\x38\x2D")
    outer = u8_entries(raw[j:])
    found = {}
    for part in ("banner.bin", "icon.bin"):
        inner = unwrap(outer["meta/" + part] if "meta/" + part in outer else outer[part])
        k = inner.find(b"\x55\xAA\x38\x2D")
        for name, data in u8_entries(inner[k:]).items():
            if data[:4] == b"\x00\x20\xAF\x30":
                for n, (fmt, img) in enumerate(decode_tpl(data)):
                    found["%s__%s__%d" % (part.split(".")[0], os.path.basename(name).replace(".tpl", ""), n)] = (fmt, img)
    return bnr, found


def trim(img):
    box = img.getchannel("A").point(lambda v: 255 if v > 24 else 0).getbbox()
    return img.crop(box) if box else img


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--disc", default=str(ROOT / "extracted" / "SO3EE9"))
    ap.add_argument("--out", default=str(ROOT / "work" / "android-build" / "branding"))
    ap.add_argument("--pick", help="source image name to use (default: automatic)")
    ap.add_argument("--list", action="store_true")
    a = ap.parse_args()
    bnr, found = sources(a.disc)
    out = Path(a.out)
    (out / "source").mkdir(parents=True, exist_ok=True)
    for n, (fmt, img) in found.items():
        img.save(out / "source" / (n + ".png"))
        print("%-40s fmt=%2d %dx%d" % (n, fmt, *img.size))
    if a.list: return
    pick = a.pick or "banner__Title_Logo__0"   # the 400x208 title logo in banner.bin
    if pick not in found:
        raise SystemExit("image %s not in the banner; see the list above" % pick)
    fmt, logo = found[pick]
    back = found.get("banner__PIC1__0", (0, None))[1]   # the underwater banner picture
    build(out, trim(logo), back, pick, bnr, logo.size)


def build(out, logo, back, pick, bnr, size):
    # Background: the logo's dominant opaque colour, darkened.
    opaque = [p for p in logo.get_flattened_data() if p[3] > 200]
    bg = (20, 90, 140)
    backsq = None
    if back is not None:
        backsq = back.crop((0, 0, back.width, back.width)).convert("RGBA")   # top square: light rays and water
        bg = tuple(sum(p[k] for p in backsq.get_flattened_data()) // (backsq.width * backsq.height) for k in range(3))
    res = out / "res"
    sizes = {"mdpi": 48, "hdpi": 72, "xhdpi": 96, "xxhdpi": 144, "xxxhdpi": 192}
    def canvas(px, frac, with_bg):
        c = Image.new("RGBA", (px, px), bg + (255,) if with_bg else (0, 0, 0, 0))
        if with_bg and backsq is not None: c = backsq.resize((px, px), Image.LANCZOS)
        s = min(px * frac / logo.width, px * frac / logo.height)
        l = logo.resize((max(1, int(logo.width * s)), max(1, int(logo.height * s))), Image.LANCZOS)
        c.alpha_composite(l, ((px - l.width) // 2, (px - l.height) // 2))
        return c
    for d, px in sizes.items():
        (res / ("mipmap-" + d)).mkdir(parents=True, exist_ok=True)
        canvas(px, 0.88, True).save(res / ("mipmap-" + d) / "ic_launcher.png")
        canvas(px * 108 // 48, 0.58, False).save(res / ("mipmap-" + d) / "ic_launcher_foreground.png")
        bgimg = backsq.resize((px * 108 // 48,) * 2, Image.LANCZOS) if backsq is not None else Image.new("RGBA", (px * 108 // 48,) * 2, bg + (255,))
        bgimg.save(res / ("mipmap-" + d) / "ic_launcher_background.png")
    (res / "mipmap-anydpi-v26").mkdir(parents=True, exist_ok=True)
    (res / "mipmap-anydpi-v26" / "ic_launcher.xml").write_text(
        '<?xml version="1.0" encoding="utf-8"?>\n<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">\n'
        '    <background android:drawable="@mipmap/ic_launcher_background" />\n'
        '    <foreground android:drawable="@mipmap/ic_launcher_foreground" />\n</adaptive-icon>\n', encoding="utf-8")
    (res / "values").mkdir(parents=True, exist_ok=True)
    (res / "values" / "app_name.xml").write_text(
        '<?xml version="1.0" encoding="utf-8"?>\n<resources>\n    <string name="app_name">%s</string>\n</resources>\n' % LABEL, encoding="utf-8")
    prev = Image.new("RGBA", (512, 256), (40, 40, 40, 255))
    prev.alpha_composite(canvas(192, 0.88, True).resize((192, 192)), (30, 32))
    ad = canvas(432, 0.58, False); full = canvas(432, 0.0001, True); full.alpha_composite(ad)
    prev.alpha_composite(full.resize((192, 192)), (290, 32))
    prev.save(out / "icon-preview.png")
    (out / "branding.json").write_text(json.dumps({"label": LABEL, "source": str(bnr), "image": pick, "size": list(size)}, indent=2))
    print("branding written to", out)


if __name__ == "__main__":
    main()
