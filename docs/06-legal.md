# 06 — Legal

## You must own the game

This project does not include, download, or distribute:

- disc images;
- extracted files;
- generated C or compiled game code;
- encryption keys, tickets, or title metadata;
- saves, textures, audio, or video.

You must dump your own disc. The build runs on your machine, from your dump, and the output
stays there.

## What can be shared

- The recompiler, runtime, and tooling source.
- Documentation, build scripts, and patches that contain no game code.
- Bug reports and logs with game data removed.
- Shader caches only if they contain no copyrighted textures or models. Prefer regenerating
  them locally.

## What must not be shared

- `game/`, `extracted/`, `generated/`, `build/`, `work/`, `analysis/`, and saves.
- The `.wrd` or any packaged image produced from a disc.
- Symbol maps derived from a retail binary, unless they contain only addresses and
  user-assigned names (still risky; keep them local).
- Any CRI/Sofdec middleware binary extracted from the disc.

`.gitignore` covers the common cases. Check `git status` before every push.

## Licensing

This project is **GPL-3.0-or-later** because the runtime will link or adapt GPL-licensed
components:

- DolRecomp — GPL-3.0-or-later.
- wii-recomp — GPL-3.0-or-later.
- Dolphin — GPL-2.0-or-later (compatible with GPL-3.0 for combined distribution).

If you copy code from any of them, keep the license and attribution. If you add a dependency
with a different license, check compatibility before linking.

## Trademarks

Rune Factory, Tides of Destiny, Oceans, Wii, Nintendo, Marvelous, and Neverland are trademarks
of their respective owners. This is an unofficial fan project. Do not imply endorsement.

## Middleware

RF:ToD ships CRI middleware (Sofdec video, CPK/ADX/HCA audio). The runtime must not
redistribute CRI binaries. Implement compatible behavior, or require the user's own disc at
runtime.

## Clean-room notes

- Do not copy decompiled game code into the repository.
- Generated C from the recompiler is game-derived; keep it out of version control.
- Patches should contain the minimum address range and logic needed, with a comment
  explaining the observed behavior.
- Prefer documenting a behavior in `analysis/` over committing a large patch.
