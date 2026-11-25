# Contributing to SeaOfStar-ToD

Thanks for helping. The most valuable contributions are test reports and small, focused fixes.

## Before you start

- Read [README.md](README.md) and [docs/08-progress.md](docs/08-progress.md) for the current state.
- Never attach or commit game files, disc images, extracted data, generated code, keys, saves,
  packed images (`*.wrd`), screenshots of game content or full logs. See [docs/06-legal.md](docs/06-legal.md).
- Code, comments, docs and commit messages are in English.

## Reporting a problem

Use the issue templates. Include the launcher build, your GPU and driver, what you did, and the
launcher's crash report (`logs\crash\<stamp>\report.txt`) with personal paths removed.

## Code and docs

- Keep a change to one concern; say how you verified it (log lines, measurements).
- Separate proven facts from hypotheses in docs.
- The runtime lives in `runtime/`, a fork of [Zontex/wii-recomp](https://github.com/Zontex/wii-recomp)
  kept as plain files (no subtree). Runtime rules: no per-game code upstream, nothing fails silently, fix
  causes by measurement against Dolphin. The maintainer develops it in a separate clone
  (`work/wii-recomp`, branch `seaofstar-tod`) and syncs it with `tools/sync-runtime.ps1`
  (`git archive` of that branch over `runtime/`); pull requests may change `runtime/` directly.
- Do not claim 60 fps or DLSS support unless it is real (ADR-007, ADR-012).

## License

By contributing you agree your work is licensed under GPL-3.0-or-later.
