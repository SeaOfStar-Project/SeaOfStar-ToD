#!/usr/bin/env python3
"""Merge shader recipes (and the function profile) from playtest runs into the shared title-data.

A parallel sweep (tools\\playtest.ps1 -Sweep -Parallel) or a sequential -Sweep run writes only its
own copy of title-data, so the shared shaders.bin/profile.txt are never touched while games run.
This tool folds those copies back in afterwards.

Sources: a run folder (work\\playtests\\<tag>-<stamp>), a title-data folder, or a shaders.bin file.
  python tools\\merge-recipes.py --dry-run work\\playtests\\sweepA-* work\\playtests\\sweepB-*
  python tools\\merge-recipes.py work\\playtests\\sweepA-20261125-230000 work\\playtests\\sweepB-20261125-230001

shaders.bin (WRSR): the 32-byte header (magic, version, record_bytes, title) of every source must
match the target; records are deduplicated by the runtime's FNV-1a recipe hash (same as
recipe-hashes.py), only unseen records are appended, in the order sources and records come.
The write is atomic (temp file + rename) after a timestamped backup (<file>.bak-<stamp>-merge).

profile.txt ("# program <hash>", then "<address|module+offset> <count>" lines): merged as a union of
functions when the program line matches; a count is the maximum seen, not a sum, because every run
copy already contains the shared counts it started from (a sum would count that base twice). If a
source has another program hash or a line that does not parse, its profile is skipped and reported.

Refuses to write the shared files while a wii-recomp.exe runs or a playtest lock is live.
--target-dir points the merge at another title-data folder (tests on copies); the running-game
check applies only to the shared one. --dry-run reads only and writes nothing.
"""
import argparse
import datetime
import glob
import json
import os
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SHARED = os.path.join(ROOT, "work", "wii-recomp", "recomp", "build", "title-data", "00010000-534f3345")
HEADER_BYTES = 32
IDENTITY_BYTES = 20  # magic, version, record_bytes, title: must match


def fnv1a(data):
    h = 2166136261
    for b in data:
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h


def read_shaders(path):
    """Returns (header bytes, record_bytes, [record bytes])."""
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < HEADER_BYTES:
        raise SystemExit("%s: too short for a shaders.bin header" % path)
    magic, version, record_bytes = struct.unpack_from("<4sII", data, 0)
    if magic != b"WRSR" or record_bytes == 0:
        raise SystemExit("%s: not a shaders.bin (magic %r)" % (path, magic))
    body = len(data) - HEADER_BYTES
    if body % record_bytes:
        raise SystemExit("%s: size %d is not header + n * %d (truncated or mid-write?)" % (path, len(data), record_bytes))
    recs = [data[HEADER_BYTES + i:HEADER_BYTES + i + record_bytes] for i in range(0, body, record_bytes)]
    return data[:HEADER_BYTES], record_bytes, recs


def resolve_source(arg):
    """Maps a run folder / title-data folder / file to (shaders.bin path, profile.txt path or None)."""
    arg = os.path.abspath(arg)
    if os.path.isfile(arg):
        prof = os.path.join(os.path.dirname(arg), "profile.txt")
        return arg, (prof if os.path.isfile(prof) else None)
    cands = [os.path.join(arg, "title-data", "00010000-534f3345"), os.path.join(arg, "00010000-534f3345"), arg]
    for c in cands:
        s = os.path.join(c, "shaders.bin")
        if os.path.isfile(s):
            prof = os.path.join(c, "profile.txt")
            return s, (prof if os.path.isfile(prof) else None)
    raise SystemExit("no shaders.bin under %s" % arg)


def pid_alive(pid):
    if pid <= 0:
        return False
    out = subprocess.run(["tasklist", "/FI", "PID eq %d" % pid, "/NH"], capture_output=True, text=True).stdout
    return str(pid) in out


def running_blockers():
    found = []
    out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq wii-recomp.exe", "/NH"], capture_output=True, text=True).stdout
    for line in out.splitlines():
        if line.lower().startswith("wii-recomp.exe"):
            found.append("wii-recomp.exe: " + " ".join(line.split()[:2]))
    locks = [os.path.join(ROOT, "work", "playtest.lock")] + sorted(glob.glob(os.path.join(ROOT, "work", "playtest-slot-*.lock")))
    for lock in locks:
        if not os.path.isfile(lock):
            continue
        try:
            with open(lock, "r", encoding="utf-8-sig") as f:
                info = json.load(f)
            pid = int(info.get("pid", 0))
            tag = info.get("tag", "?")
        except (ValueError, OSError):
            found.append("%s (unreadable, treated as live)" % lock)
            continue
        if pid_alive(pid):
            found.append("%s (pid %d, tag %s)" % (os.path.basename(lock), pid, tag))
    return found


def atomic_write(path, data, stamp):
    """Backs the old file up, writes a temp file next to it, then renames over it."""
    backup = None
    if os.path.exists(path):
        backup = "%s.bak-%s-merge" % (path, stamp)
        with open(path, "rb") as src, open(backup, "wb") as dst:
            dst.write(src.read())
    tmp = "%s.merge-tmp-%d" % (path, os.getpid())
    with open(tmp, "wb") as f:
        f.write(data)
        f.flush()
        os.fsync(f.fileno())
    os.replace(tmp, path)
    return backup


def merge_shaders(target_path, sources, dry_run, stamp):
    thead, trb, trecs = read_shaders(target_path)
    seen = {}
    for r in trecs:
        seen.setdefault(fnv1a(r), r)
    before = len(trecs)
    new_records = []
    print("shaders.bin: target %s" % target_path)
    print("  before: %d records (%d bytes each)" % (before, trb))
    for src, _ in sources:
        shead, srb, srecs = read_shaders(src)
        if shead[:IDENTITY_BYTES] != thead[:IDENTITY_BYTES] or srb != trb:
            raise SystemExit("%s: header mismatch (magic/version/record_bytes/title) with the target; refusing to merge" % src)
        if shead[IDENTITY_BYTES:] != thead[IDENTITY_BYTES:]:
            print("  [warn] %s: header bytes 20..31 differ from the target (ignored)" % src)
        added = dup = clash = 0
        for r in srecs:
            h = fnv1a(r)
            if h in seen:
                if seen[h] != r:
                    clash += 1
                else:
                    dup += 1
                continue
            seen[h] = r
            new_records.append(r)
            added += 1
        print("  source %s: candidates %d, already known %d, new %d%s" % (
            src, len(srecs), dup, added, (", hash clashes skipped %d" % clash) if clash else ""))
    after = before + len(new_records)
    print("  summary: before %d, new %d, after %d" % (before, len(new_records), after))
    if not new_records:
        print("  nothing to append")
        return 0
    if dry_run:
        print("  dry run: not written")
        return len(new_records)
    data = thead + b"".join(trecs) + b"".join(new_records)
    backup = atomic_write(target_path, data, stamp)
    _, _, check = read_shaders(target_path)
    if len(check) != after:
        raise SystemExit("verification failed: %d records after write, expected %d (backup %s)" % (len(check), after, backup))
    print("  written (backup %s)" % backup)
    return len(new_records)


def parse_profile(path):
    """Returns (program hash, ordered dict key -> count) or raises ValueError."""
    with open(path, "r", encoding="utf-8") as f:
        lines = f.read().splitlines()
    if not lines or not lines[0].startswith("# program "):
        raise ValueError("no '# program <hash>' first line")
    program = lines[0][len("# program "):].strip()
    counts = {}
    for n, line in enumerate(lines[1:], 2):
        if not line.strip():
            continue
        parts = line.split()
        if len(parts) != 2 or not parts[1].isdigit():
            raise ValueError("line %d does not parse: %r" % (n, line))
        key = parts[0]
        counts[key] = max(counts.get(key, 0), int(parts[1]))
    return program, counts


def key_order(key):
    # plain hex addresses first (ascending), then "<module>+<hex offset>" grouped by module name.
    if "+" in key:
        mod, off = key.rsplit("+", 1)
        return (1, mod, int(off, 16))
    return (0, "", int(key, 16))


def merge_profile(target_dir, sources, dry_run, stamp):
    tpath = os.path.join(target_dir, "profile.txt")
    print("profile.txt: target %s" % tpath)
    if not os.path.isfile(tpath):
        print("  [skip] target has no profile.txt")
        return
    try:
        program, counts = parse_profile(tpath)
    except ValueError as e:
        print("  [skip] target profile.txt is not mergeable: %s" % e)
        return
    first_keys = list(counts)
    before = len(counts)
    changed = False
    print("  before: %d functions, program %s" % (before, program))
    for _, prof in sources:
        if not prof:
            print("  source: no profile.txt next to the shaders.bin (skipped)")
            continue
        try:
            sprogram, scounts = parse_profile(prof)
        except ValueError as e:
            print("  [skip] %s: %s" % (prof, e))
            continue
        if sprogram != program:
            print("  [skip] %s: program %s differs from the target's %s" % (prof, sprogram, program))
            continue
        new = raised = 0
        for k, v in scounts.items():
            if k not in counts:
                counts[k] = v
                new += 1
            elif v > counts[k]:
                counts[k] = v
                raised += 1
        print("  source %s: %d functions, new %d, higher count %d" % (prof, len(scounts), new, raised))
        changed = changed or new or raised
    print("  summary: before %d, after %d (counts merged as the maximum, see --help)" % (before, len(counts)))
    if not changed:
        print("  nothing to change")
        return
    if dry_run:
        print("  dry run: not written")
        return
    keys = sorted(counts, key=key_order)
    text = "# program %s\n" % program + "".join("%s %d\n" % (k, counts[k]) for k in keys)
    backup = atomic_write(tpath, text.encode("utf-8"), stamp)
    print("  written (backup %s)" % backup)


def main():
    ap = argparse.ArgumentParser(description="Merge recipes and the function profile from playtest run copies into the shared title-data.")
    ap.add_argument("sources", nargs="+", help="run folders, title-data folders or shaders.bin files")
    ap.add_argument("--dry-run", action="store_true", help="read and report only, write nothing")
    ap.add_argument("--target-dir", default=SHARED, help="title-data folder to merge into (default: the shared one)")
    ap.add_argument("--no-profile", action="store_true", help="leave profile.txt alone")
    args = ap.parse_args()

    target_dir = os.path.abspath(args.target_dir)
    target = os.path.join(target_dir, "shaders.bin")
    if not os.path.isfile(target):
        raise SystemExit("no target shaders.bin at %s" % target)
    sources = []
    for a in args.sources:
        s = resolve_source(a)
        if os.path.abspath(s[0]) == os.path.abspath(target):
            raise SystemExit("source %s is the target itself" % s[0])
        sources.append(s)

    shared = os.path.normcase(target_dir) == os.path.normcase(SHARED)
    blockers = running_blockers() if shared else []
    if blockers:
        if args.dry_run:
            print("[warn] a game or playtest is running; this dry run only reads: " + "; ".join(blockers))
        else:
            raise SystemExit("refusing to write the shared title-data while something runs: " + "; ".join(blockers))

    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    merge_shaders(target, sources, args.dry_run, stamp)
    if not args.no_profile:
        merge_profile(target_dir, sources, args.dry_run, stamp)


if __name__ == "__main__":
    main()
