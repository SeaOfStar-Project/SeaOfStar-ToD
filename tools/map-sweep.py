#!/usr/bin/env python3
"""Generate playtest timelines that warp through map IDs (shader coverage sweep).

Each timeline loads a save (the prefix), then for every map ID writes the map manager's
request words and calls the post function, waits for the load, reads the current map, and
takes a shot, walks with the stick, and takes a second shot. 12.5 s per map.

The addresses are SO3EE9 (USA) specific. They come from analysis/map-warp-notes.md
(gitignored): map manager 0x805F9138, request words +0x7E0 (new id), +0x7E4 (previous id),
+0x7E8 (0xffffffff), post function 0x8012E234, current map getter 0x8000EE60.
IDs 4xx/5xx do not warp (they are progress variants). ID 186 faults in guest code on load
and is skipped by default.

Usage:
  map-sweep.py --out DIR                  # all chunks as DIR/cov<N>.txt
  map-sweep.py --chunk 0 [--out DIR]      # one chunk (stdout when no --out)
  map-sweep.py --ids 0-9,20,30-33 --out DIR
  map-sweep.py --prefix FILE ...          # prefix lines (save loading) from FILE

Run a timeline with tools\\playtest.ps1 (see docs/04-tooling.md); -Coverage writes the shared
shaders.bin, so back it up first.
"""
import argparse
import os
import sys

# Timeline lines that load the save and reach gameplay (slot 11 via the Load list).
DEFAULT_PREFIX = [
    "52.0 press a",
    "55.0 press down",
    "58.0 press a",
    "60.5 shot a_list.ppm",
    "62.0 stick 0 1",
    "62.3 stick 0 0",
    "63.5 shot b_stickup.ppm",
    "65.0 press a",
    "66.5 shot c_confirm.ppm",
    "68.0 press a",
    "72.0 shot d_72.ppm",
    "77.0 shot d_77.ppm",
    "82.0 shot d_82.ppm",
    "87.0 shot d_87.ppm",
    "92.0 shot d_92.ppm",
]

BASE_RANGES = [(0, 9), (20, 29), (30, 33), (40, 46), (50, 53), (100, 124), (150, 155),
               (160, 166), (170, 177), (180, 188), (190, 193)]
SKIP = {186}            # guest fault on load
START_T = 95.0          # first warp, seconds after the prefix
PER_MAP = 12.5
FIRST_PREV = 0x17       # map the save starts in (previous-id word of the first warp)
MAP_MGR = "805f9138"
REQ_NEW, REQ_PREV, REQ_FLAG = "805f9918", "805f991c", "805f9920"
POST_FN, GETTER_FN = "8012e234", "8000ee60"


def parse_ids(text):
    ids = []
    for part in text.split(","):
        part = part.strip()
        if not part:
            continue
        if "-" in part:
            a, b = part.split("-", 1)
            ids.extend(range(int(a), int(b) + 1))
        else:
            ids.append(int(part))
    return ids


def fmt(tenths):
    s = "%.1f" % (tenths / 10.0)
    return s[:-2] if s.endswith(".0") else s


def timeline(prefix, ids, chunk_no, prev_id):
    out = list(prefix)
    t = int(round(START_T * 10))
    for mid in ids:
        def add(dt, text):
            out.append("%s %s" % (fmt(t + dt), text))
        add(0, "poke %s 4 %08x" % (REQ_NEW, mid))
        add(3, "poke %s 4 %08x" % (REQ_PREV, prev_id))
        add(6, "poke %s 4 %08x" % (REQ_FLAG, 0xFFFFFFFF))
        add(9, "call %s %s" % (POST_FN, MAP_MGR))
        add(70, "call %s %s" % (GETTER_FN, MAP_MGR))
        add(73, "shot c%d_%d_a.ppm" % (chunk_no, mid))
        add(76, "stick 0 1")
        add(96, "stick 0 0")
        add(98, "stick 1 0")
        add(110, "stick 0 0")
        add(113, "shot c%d_%d_b.ppm" % (chunk_no, mid))
        t += int(round(PER_MAP * 10))
        prev_id = mid
    return "\n".join(out) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--ids", help="comma list with ranges, e.g. 0-9,20,30-33 (default: base IDs)")
    ap.add_argument("--chunk", type=int, help="emit only this chunk index (default: all)")
    ap.add_argument("--chunk-size", type=int, default=24, help="maps per timeline (default 24)")
    ap.add_argument("--out", help="output directory (files cov<N>.txt)")
    ap.add_argument("--prefix", help="file with the prefix lines instead of the built-in one")
    args = ap.parse_args()

    if args.ids:
        ids = parse_ids(args.ids)
    else:
        ids = [i for a, b in BASE_RANGES for i in range(a, b + 1) if i not in SKIP]
    prefix = DEFAULT_PREFIX
    if args.prefix:
        with open(args.prefix, encoding="utf-8") as f:
            prefix = [l.rstrip("\r\n") for l in f if l.strip()]

    size = args.chunk_size
    chunks = [ids[i:i + size] for i in range(0, len(ids), size)]
    wanted = range(len(chunks)) if args.chunk is None else [args.chunk]
    if args.chunk is None and not args.out:
        sys.exit("give --out DIR (or --chunk N to print one chunk)")
    for n in wanted:
        if n >= len(chunks):
            sys.exit("chunk %d does not exist (%d chunks)" % (n, len(chunks)))
        text = timeline(prefix, chunks[n], n, FIRST_PREV)
        if args.out:
            os.makedirs(args.out, exist_ok=True)
            path = os.path.join(args.out, "cov%d.txt" % n)
            with open(path, "w", encoding="utf-8", newline="\n") as f:
                f.write(text)
            print("%s: %d maps, ends at %ss" % (path, len(chunks[n]), fmt(int(round(START_T * 10)) + int(round(PER_MAP * 10)) * len(chunks[n]))))
        else:
            sys.stdout.write(text)


main()
