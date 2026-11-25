# 04 — Tooling

## Current toolchain

| Tool | Version | Role | Status |
|---|---|---|---|
| Visual Studio 2022 Build Tools | 17.14 / MSVC 19.44 | Host compiler | Installed |
| CMake | 4.x | Build system | Installed |
| Ninja | 1.x | Optional generator | Installed |
| Python | 3.12 | Tooling scripts | Installed |
| nodtool | 2.0.0-alpha.12 | Disc extraction | Downloaded, hash verified |
| DolRecomp | `02e27aca` | PowerPC → C | Built, recompiles this game |
| wii-recomp | `55fb408e` | Full Wii runtime | Cloned; Windows build needs MSYS2 CLANG64 |
| MSYS2 | installed | CLANG64 toolchain for wii-recomp | clang 23.1.5, ninja 1.13.2, GLFW 3.5.1, SDL3, Vulkan, shaderc installed |
| Dolphin | — | Reference oracle | Not detected; install manually |

## DolRecomp

Repository: <https://github.com/ExpansionPak/DolRecomp>

Built by `tools/fetch-tools.ps1` into `work/DolRecomp/build/Release/dolrecomp.exe`.

### Wii mode requires a title database

DolRecomp falls back to GameCube mode when `database/titles.txt` is missing. In that mode it
uses the Gekko profile and writes `generated/generated/generated.c`.

`tools/recompile.ps1` seeds `database/titles.txt` with this project's title:

```
SO3EE9 = Rune Factory: Tides of Destiny
```

With the seed present, the tool selects the Broadway profile and writes
`generated/SO3EE9_generated/SO3EE9.c`.

To use the full GameTDB database instead, replace `database/titles.txt` with the file from
`https://www.gametdb.com/titles.txt?LANG=EN` (or run `dolrecomp --setup`).

### Commands

```powershell
# CPU recompilation (Wii)
work\DolRecomp\build\Release\dolrecomp.exe `
    extracted\SO3EE9\sys\main.dol SO3EE9 generated

# Optional linker MAP for readable symbol names
work\DolRecomp\build\Release\dolrecomp.exe `
    --map path\to\main.map extracted\SO3EE9\sys\main.dol SO3EE9 generated

# Patch config
work\DolRecomp\build\Release\dolrecomp.exe `
    --config path\to\patches.toml extracted\SO3EE9\sys\main.dol SO3EE9 generated

# REL folder (not used by this game)
work\DolRecomp\build\Release\dolrecomp.exe extracted\SO3EE9\files\rel SO3EE9 generated
```

### Result for this build

| Metric | Value |
|---|---|
| CPU profile | Broadway (Wii) |
| Entry point | `0x80004050` |
| Instructions decoded | 1,353,840 |
| Unknown instructions | 0 |
| Embedded data in text | 4,985 |
| Chunks | 331 |
| Generated C | 337 MB |
| Self-modifying-code candidates | 17 ranges |

The recompiler reports no decode failures. That is the CPU-side green light: the remaining
work is runtime, graphics, audio, and input.

### Output shape

One guest function becomes one host function, with big-endian memory helpers:

```c
// 80004130: lwz r7, 0(r6)
{
    u32 ea = ctx->gpr[6] + (u32)(s32)(0);
    ctx->gpr[7] = mem_read32(ctx, ea);
}

// 80004134: add r7, r7, r5
{
    u32 a = ctx->gpr[7];
    u32 b = ctx->gpr[5];
    ctx->gpr[7] = a + b;
}
```

## wii-recomp

Repository: <https://github.com/Zontex/wii-recomp>

The most complete existing Wii runtime: PowerPC → C translator, interpreter fallback, GX on
Vulkan, AX mixer, IOS HLE, simulated Wii Remote + Nunchuk, RSO/REL module support.

### Windows requirements

From `docs/windows.md`:

```sh
winget install MSYS2.MSYS2
```

Then, from an MSYS2 CLANG64 shell:

```sh
pacman -S --needed mingw-w64-clang-x86_64-{clang,lld,ninja,glfw,sdl3,vulkan-headers,vulkan-loader,shaderc}
```

On this machine `mirror.msys2.org` redirects to a regional mirror that stalled; pointing
`/etc/pacman.d/mirrorlist.*` at `https://repo.msys2.org/mingw/$repo/` fixed it. The packages
already cached under `/var/cache/pacman/pkg` are reused.

```powershell
# One-shot boot test against the extracted tree (no 2 GB image packing)
powershell -NoProfile -ExecutionPolicy Bypass -File tools\wii-recomp-boot.ps1 -Seconds 60
```

The script translates the DOL, builds the console and `title.dll`, then runs the console for
the requested time with `WII_RECOMP_ACTIVITY`, `WII_RECOMP_FPS_LOG`, `WII_RECOMP_FRAME_LOG`
and `WII_RECOMP_DISC_LOG` enabled, and writes `logs/wii-recomp-run.log`.

### Usage

```bat
python recomp\tools\add_game.py "path\to\game.rvz"
recomp\build\wii-recomp.exe games\<game ID>.wrd
```

`add_game.py` extracts the disc, translates all code, builds the console if needed, compiles
the game, and packs a `.wrd`. The first build is slower because it also instruments code for
the second, profile-guided build.

A NAND folder is required for Wii Menu mode. Direct disc play may work without one, but some
games need system files; `recomp/build/saves` is created on first start.

### Why it matters here

wii-recomp already implements the pieces this project would otherwise write from scratch:
GX command decode, TEV shaders, EFB/XFB, AX, IOS, WPAD. On this disc it boots through the
safety screen, logos, the Sofdec opening movie (with audio), the title screen, and a New
Game load into a 3D gameplay scene. It is GPL-3.0-or-later, so code can be reused in this
project with attribution.

Because the baseline reaches gameplay, wii-recomp is now the runtime base rather than a
disposable reference (ADR-010). The practical build is:

```powershell
python recomp\tools\add_game.py <extracted disc> --no-pack --jobs 8
# title.dll lands in recomp\build-<ID>\
recomp\build\wii-recomp.exe <extracted disc>\sys
```

`tools/wii-recomp-boot.ps1` wraps this, sets `WII_RECOMP_NATIVE`, and can request a
screenshot through the `wii` control pipe. Full results: `analysis/wii-recomp-baseline.md`.

The pre-baked variant drops `--no-pack` (or runs `tools\wii-recomp-build.ps1`, which is the
launcher's **Build** page): the disc, `title.dll` and `shaders.bin` are packed into
`games\<ID>.wrd`, which boots on its own. Measurements: `analysis/prebake.md`.

`wii-recomp-build.ps1` takes an optional `-Disc` path (the extracted root or its `sys`
folder) so the launcher's Game files page decides which disc is built; without it the
script uses `paths.extracted` from `config/game.json`.

### Launcher

`launcher/` is a native Win32 companion that starts the same console and exposes the
graphics options. Build it with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\build-launcher.ps1
build\launcher\SeaOfStar-ToD.exe
```

It reads `config/launcher.ini` (created with defaults on first run) and follows the page
flow of the reference recompilation launchers:

- **Home** is task-first. The primary button is Play when `Ready()` and Set up otherwise,
  with the packed image, recipe count, display shape and frame rate in a summary card.
  Game files, Rebuild, Controls, Saves, Logs and Open config sit below it.
- **Settings** opens from the gear: Display, Graphics and In-game cards. Render scale,
  window size, present mode, fullscreen, FSR 1.0 with sharpness, and the FPS overlay are
  persisted to the same ini.
- **Game files** owns the image, extracted disc, native library and console paths, each
  with a Use and an Open button; the console has its own picker.
- **Build** runs `tools\wii-recomp-build.ps1` as a hidden child process, tails
  `logs\launcher-build.log` every 300 ms and maps the script's `== section` markers to a
  progress bar. Back leaves it running; the header pill says BUILDING until the exit code
  arrives.

Setup state: the launcher computes four steps (game dump, console exe, `title.dll`, packed
image) at startup, after a build, after the game exits and when a path changes, and writes them
to `config\setup-state.json` (per-step `done`, paths, ISO 8601 mtimes, recipe count, build
stamp). The image is `missing`, `current`, `stale_title_newer` (older than `title.dll`) or
`stale_new_recipes` (older than `shaders.bin`; `recipes.new_since_image` is the count when the
image was last seen current, else `null`). While the image is stale Home shows a one-line reason
and a Rebuild button (same as the Build page); Play stays enabled and uses the loose files.
`SeaOfStar-ToD.exe --write-setup-state` writes the file without a window and exits 0 (3 if the
write failed).

Play passes the packed image when it is at least as new as the library and recipes, and
otherwise the extracted tree with `WII_RECOMP_NATIVE` set. The game's stdout and stderr go
to `logs\play\<yyyyMMdd-HHmmss>.log` through an inherited file handle (no console window,
no pipe to drain); the newest 20 logs are kept, the status line shows the path, and the
Home **Last log** button opens the newest one. The launcher also accepts a
dragged `.wrd`, disc folder or `wii-recomp.exe` (Windows 10/11 file-drop messages), and
has `--settings`, `--gamefiles`, `--build-page`, `--build`, `--controls` and
`--input-test` for testing a page directly.

Controls window: tabs Keyboard, Gamepad, Mouse (`[mouse]`) and Player (`[player1]`), every
change written to `controllers.ini` beside the console at once. `SeaOfStar-ToD.exe
--dump-controls [file]` loads that file (or the console's own) the way the editor does,
prints every mapped value and writes `logs\controls-dump.txt`, without a window; exit 0
(3 if the dump could not be written).

Crash report: when the game started by Play exits, the launcher classifies the session. It is
normal only if the exit code is 0 and the play log holds the runtime's `console: turning off`
line; anything else is unexpected. An unexpected session writes
`logs\crash\<stamp>\report.txt` (launcher build, time, exit code in decimal and hex, the reason,
`[options]` from `config\launcher.ini`, `config\setup-state.json`, the log's `vulkan: ` GPU line
and the last 200 log lines; the newest 20 folders are kept) and Home shows "The last session
ended unexpectedly" with **Open report** and **Dismiss** (it takes the slot of the stale-image
notice until dismissed). Either button stores the stamp in `config\launcher.ini` `[crash] seen=`.
Nothing is uploaded: the report holds user paths, so the user attaches it by hand.
`SeaOfStar-ToD.exe --check-session <log> <exit code>` runs the same classification without a
window, prints it and writes `logs\crash\last-check.txt`.

### Video options

| Variable | Meaning | Default |
|---|---|---|
| `WII_RECOMP_SCALE` | Internal render multiplier over 640×480, whole numbers or a fraction (`3/2`) | `1.5` on the launcher |
| `WII_RECOMP_WINDOW` | Starting window, `WxH`; the console's 4:3/16:9 setting from SYSCONF is applied as an aspect hint, which can resize it | `1280x720` on the launcher |
| `WII_RECOMP_FULLSCREEN` | Start full screen (`1`) | windowed |
| `WII_RECOMP_PRESENT` | `fifo`, `mailbox`, or `immediate` | first supported; the launcher chooses `fifo` |
| `WII_RECOMP_FSR` | FSR 1.0 upscale (`1`/`0`) | off in the runtime, on in the launcher |
| `WII_RECOMP_FSR_SHARPNESS` | RCAS strength, 0 sharpest to 2 softest | `0.2` |
| `WII_RECOMP_NGX` | Start NVIDIA's NGX core and run the DLSS experiment (create plus one synthetic evaluation); no game frame is upscaled | off |
| `WII_RECOMP_NGX_LOG` | Copy the NGX core's log to stderr (`1`) | off |

FSR 1.0 is implemented in the runtime, not in the launcher: EASU scales the finished
EFB picture and RCAS sharpens it before the window image is presented. When the window
is smaller than the picture the runtime keeps the linear blit, because a downscale has
nothing for EASU to reconstruct. Results and frame-time notes: `analysis/fsr.md`.

The NGX variables are an experiment, not a graphics option: the feature lifecycle runs,
but the game supplies no motion vectors, jitter or native 16:9 picture, and the launcher
therefore has no DLSS toggle. ADR-012 has the constraints.

## ModernGekko-Template

Repository: <https://github.com/ExpansionPak/ModernGekko-Template>

DolRecomp plus a Dolphin-derived runtime. Good reference for integrating generated C with
VideoCommon, but DolRecomp's README states that ModernGekko does not build under MSVC
currently; it needs a MinGW/Clang environment. Treat it as a reference, not the primary path.

## Other projects

| Project | Notes |
|---|---|
| NWiiRecomp | C++ runtime, paired singles + GQR, interpreter fallback, OpenGL |
| gcrecomp | GameCube-first, D3D11, Wii support incomplete |
| Wii Party Recomp | Playable reference; 115 REL modules, recompiled audio microcode |
| Mario Kart Wiicompiled | First Wii static recomp; useful integration notes |

## Host prerequisites

- Windows 10/11 x64.
- Vulkan 1.3 GPU for the renderer.
- 16 GB RAM; 60+ GB free for a full build with a Dolphin-derived runtime.
- Your own disc dump. Nothing is downloaded by this repository.

## Playtests (tools\playtest.ps1)

`tools\playtest.ps1` is the only way to start the game for a test (one run at a time, own folder per run). It refuses to run while `wii-recomp.exe`, the launcher,
`ninja`, `clang`, a build script or another playtest is running, and takes
`work\playtest.lock` (created atomically; a lock whose PID is gone is stale and cleared).

```powershell
tools\playtest.ps1 -Tag boot1 -Seconds 240 -Every 5             # boot, a picture every 5 s
tools\playtest.ps1 -Tag loadprobe -Script logs\loadprobe.txt     # timeline, same format as play-script.ps1
tools\playtest.ps1 -Tag cov -Coverage -Seconds 600              # writes the shared title-data
tools\playtest.ps1 -Tag sweepA -Sweep -Parallel -Script work\sweep\cov0.txt   # up to 3 at once, own title-data copy each
python tools\merge-recipes.py --dry-run work\playtests\sweepA-* work\playtests\sweepB-*   # then without --dry-run (nothing running)
tools\playtest.ps1 -Tag x -DryRun                               # all checks + plan, starts nothing
tools\playtest.ps1 -Tag tree1 -Tree -Seconds 60                 # boot the extracted tree + WII_RECOMP_NATIVE like the launcher
tools\playtest.ps1 -Tag idle1 -Seconds 70 -NoControl            # no control pipe (idle run, no shots or script)
```

Parameters: `-Tag` (required), `-Seconds` (default 120, or last timeline time + 3),
`-Script`, `-Every` (picture interval, 0 = none), `-Window` (default windowless),
`-Coverage` (use the shared `title-data`; otherwise a private copy), `-Pristine <stamp>`
(default: newest folder under `work\wii-recomp\recomp\build\saves\pristine`), `-DryRun`,
`-NoControl` (do not create the control pipe; for idle frame-time runs, so no `-Script` or shots), `-Tree` (boot the extracted tree with `WII_RECOMP_NATIVE`, as the launcher does, instead of the
packed image; **known bug:** the game closes itself ~2 s after boot under `-Tree`, "console: the
window was asked to close" before the first frame, cause unknown).

Each run gets `work\playtests\<tag>-<stamp>\` with `run.err.log`, `run.out.log`, `shots\`,
`saves\` (seeded from the pristine copy, hashes checked against its `manifest.sha256`),
`title-data\` (unless `-Coverage`) and `summary.json` (tag, start/end, exit reason, pristine
stamp, save hashes before and after, shot count). The control pipe name is fresh per run, the
process tree is killed and the lock released in a `finally` block, also on Ctrl+C. The cleanup is
hardened: the `finally` block can no longer throw, the lock is always released, and the original
error is printed.

**Pipe replies and memory verbs.** The runtime's control pipe is two-way on Windows. The script
sends `reply on` first; the runtime then answers every command with exactly one line (`ok ...` or
`error ...`). The script prints it as `  < <reply>` under each `  > verb` line and appends
`time<TAB>command<TAB>reply` to `<run>\pipe.log` (`<no reply>` after 2 s; `<replies off>` if
the runtime is too old to answer `reply on`). Verbs for looking into the game, all numbers hex:
`peek <addr> <words>` (reply: the words on one line), `poke <addr> <1|2|4> <value>` (writes
big-endian guest memory, same address form as `peek`, MEM1 and MEM2 only; logs
`control: poke 0x... w=4 old=0x... new=0x...` to stderr) `pokef <addr> <float>` (writes a 32-bit big-endian IEEE float, decimal
value, same checks and `control: pokef ...` log as `poke`; handy for positions) and
`call <addr> [r3 .. r10]` (runs the translated guest function at `<addr>` with up to 8 hex arguments
in r3 to r10, replies `ok call <addr> r3=<result>`,
logs `control: call ...`; an address with no translated function is an error). `poke` and `call`
run on the game's thread at its next interrupt check (a few milliseconds), and the reply comes
after. `call` runs only while the game has interrupts on, with them off during the call, on the
stack below r1, and puts the whole processor state back afterwards. It can't call a function
that waits, sleeps or switches threads: it would never return, the reply is `error ... did not
return` and the log says the context switched. Calling is safe only for short leaf-like
functions (getters, flag setters).

**Timeline variables.** A timeline line may end with `-> NAME` to keep a value from its reply: for
`call` the result r3, for `peek` the first word (further words as `NAME_1`, `NAME_2`, ...). Later
arguments may use `$NAME`, `$NAME+0x34` or `$NAME-8` (an offset with `0x` is hex, a bare one is
decimal; 32-bit wrap-around); each is replaced before sending by 8-digit lowercase hex without `0x`,
like the addresses written by hand. The substituted command is what is printed and written to
`pipe.log`. A line that uses a variable nothing captured (or whose capture failed) logs an error,
is not sent, and the timeline goes on. `-DryRun` lists the lines that use variables unsubstituted
and warns about uses with no earlier capture. Example:
`10 call 80123456 -> OBJ`, `11 pokef $OBJ+0x34 1.5`.

**Map sweep and recipe hashes (shader coverage).** `tools\map-sweep.py` writes playtest
timelines that load a save and warp through map IDs with the `poke`/`call` verbs (12.5 s per
map; the addresses are SO3EE9 (USA) specific; 4xx/5xx IDs do not warp, ID 186 is skipped). Usage:

```powershell
python tools\map-sweep.py --out work\sweep                  # cov0.txt .. cov3.txt, 24 maps each
python tools\map-sweep.py --chunk 0                          # one chunk to stdout
python tools\map-sweep.py --ids 0-9,20,30-33 --out work\sweep  # custom IDs; --prefix FILE swaps the save-loading prefix
copy work\wii-recomp\recomp\build\title-data\00010000-534f3345\shaders.bin shaders.bin.bak   # -Coverage writes the shared file: back it up first
tools\playtest.ps1 -Tag cov0 -Coverage -Script work\sweep\cov0.txt
python tools\recipe-hashes.py shaders.bin                    # recipe hash per record, matches "recipe=<8 hex>" in miss logs
```

Rebuild the packed image afterwards (`shaders.bin` newer than the image makes Play fall back to
the extracted tree). Results: `docs/08-progress.md`, "Map sweep shader coverage".

**Parallel sweeps and offline merge.** Shader-coverage sweeps may run up to 3 windowless instances in parallel with `tools\playtest.ps1 -Sweep -Parallel`; each writes its own title-data copy, merged afterwards with `tools\merge-recipes.py`; frame-time measurements are never taken from parallel runs (user decision 2026-11-25).
`-Parallel` requires `-Sweep` and is refused with `-Coverage`, `-Every > 0` and `-Window`. It takes the first free
`work\playtest-slot-<N>.lock` (PID, tag, start, mode, child `wii-recomp.exe` PID; stale by PID like the main lock),
is refused while the exclusive `work\playtest.lock` is held, and a normal run is refused while any slot lock is live. Only
`wii-recomp.exe`/`playtest.ps1` processes that belong to a live slot lock are tolerated; ninja, clang, the launcher and
any other game still refuse. `-Sweep` alone is a normal exclusive run that also writes only its own copy. Each run's recipes
accumulate in `<run>\title-data\00010000-534f3345\shaders.bin`; the shared `shaders.bin`/`profile.txt` are never
written by a sweep. `tools\merge-recipes.py <run folders...>` folds them in: header (magic, version, record bytes,
title) must match, records are deduplicated by the FNV-1a recipe hash (as `recipe-hashes.py`), new ones are appended in
order, written atomically after a `.bak-<stamp>-merge` backup, and it prints before/candidates/new/after.
`profile.txt` is merged as a union of functions with the maximum count (each run copy already holds the shared counts,
so summing would double count them); another program hash or an unparsable line skips it. It refuses while a
`wii-recomp.exe` or a playtest lock is live (`--dry-run` only reads; `--target-dir` merges into a copy for tests).

**Graphics settings.** By default a run reads `config\launcher.ini` `[options]` (fsr, sharpness,
scale, window, present; fullscreen ignored) and sets the same `WII_RECOMP_*` variables as the
launcher (scale 1.5 -> `3/2`, 2.5 -> `5/2`, else a whole number). `-Scale <s>` overrides the
scale (e.g. `2`, `3/2`); `-RuntimeDefaults` skips the ini (runtime defaults, render scale 2). A
`WII_RECOMP_*` variable already set by the caller wins over the ini. The plan prints each value
with its source and `summary.json` records them. `WII_RECOMP_FPS_OVERLAY=0` stays so shots are clean.

**Frame times (measured 2026-11-24, 70 s idle at the title).** The runtime's default scale 2
gives 82.6 frames >50 ms per minute; `WII_RECOMP_SCALE=3/2` alone 26.0; the full launcher
environment (fifo, 1280x720, FSR 0.20, 3/2, windowed) 34.3. A fresh vs copied `shared2` made no
difference (82.6 vs 81.3). Cause: at scale 2 the test GPU needs slightly more than
33.3 ms per frame, so about one frame a second is doubled. It is GPU load, not the harness. Use
the default (ini) mode for frame-time work; `-RuntimeDefaults` or `-Scale 2` reproduce the
scale-2 behaviour. Further runs (70 s idle from boot, >50 ms frames per minute after skipping the first 600 frames;
proven): scale 2 102.5 (p95 50.2 ms); launcher settings with control pipe 25.7 (p95 38.2); same
with `-NoControl` 19.1 (p95 35.7), a difference within run-to-run noise; scale 1 with launcher
settings 28.3 (p95 38.8), so below 1.5 the scale no longer matters; live vs fresh `shared2` 81.3 vs
82.6 (no effect). Open: idle runs sit on the opening movie and attract loop, so they are not
like-for-like with free play.

**World-state sweep recipe (2026-11-26).** To collect recipes per condition without playing: load a save, then poke the calendar object `P = *(0x805EAA48)` (season, week, day, weekday, hour bytes; weather at `P+0x64`; the word at `0x805EAA94` freezes the clock) and warp through the maps (`tools\map-sweep.py` timelines; timeline variables `-> NAME` and `$NAME+off` carry addresses between lines). Conditions used: seasons x 10:00/20:00 over 93 maps, weather 1-4 on 20 outdoor maps, dusk 17:00. Festivals are derived from the date (flags 0x346..0x356 have no storage): set the date, then warp, and confirm the event id with `call 8021ae40`. The flag setter works only for flags with set scripts. Battle sweep: warp to dungeon maps with natural enemies (`analysis\battle-notes.md`) and attack with button presses so magic and damage numbers render; never `call` the monster factory at 0x80075e0c (it blocks). Run with `-Sweep -Parallel` (up to 3), merge with `tools\merge-recipes.py` when nothing runs, rebuild the image. Verify with fresh shader-cache folders (3 parallel runs): 2026-11-26 result was prebake 617 recipes / 296 programs / 617 pipelines in 314-381 ms, with 4, 1 and 0 misses on the spring-maps, festival and battle routes. Growth 550 -> 617 is in docs/08-progress.md.
