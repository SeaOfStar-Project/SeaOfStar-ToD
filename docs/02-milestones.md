# 02 — Milestones

Starting state: **M0 complete**. Every milestone has clear acceptance criteria; do not move
on until the previous one is stable.

Current snapshot: [08-progress.md](08-progress.md) (2026-11-24).

| ID | Milestone | Status |
|---|---|---|
| M0 | Scaffold: repo, docs, scripts | ✅ Done |
| M1 | Environment + toolchain proven | ✅ Done |
| M2 | Disc extracted + manifest | ✅ Done |
| M3 | CPU recompilation succeeds | ✅ Done |
| B1 | Baseline boot through wii-recomp | ✅ Done — boots to gameplay |
| M4 | Boot `OSInit` + logging | ✅ Covered by wii-recomp |
| M5 | VI + black screen | ✅ Covered by wii-recomp |
| M6 | GX 2D (logo/menu) | ✅ Covered by wii-recomp |
| M7 | GX 3D (intro/gameplay) | ✅ Covered by wii-recomp |
| M8 | Input + main loop | ✅ Covered by wii-recomp |
| M9 | Audio | ✅ Covered by wii-recomp |
| M10 | Save/load | 🔄 Extractor bug fixed; `shift80` save loads from the in-game Load menu (slot 12); clean re-extract pending |
| M11 | 30 minutes of stable gameplay | ✅ Done — 34 minutes, alive at end |
| M12 | Full playthrough | ⬜ Not attempted |
| M13 | Polish + launcher | 🔄 Launcher, controls, packed image and FSR done; widescreen and packaging pending |

---

## M0 — Scaffold

**Goal:** repo, docs, and entry-point scripts are ready.

- [x] README, `.gitignore`, `.gitattributes`, `.editorconfig`
- [x] `config/game.example.json`
- [x] `tools/` (check, fetch, extract, recompile, build, run, status)
- [x] `runtime/` (the wii-recomp fork, ADR-010) and `launcher/`
- [x] Docs 00–07

**Acceptance:** `tools/check-env.ps1` runs; `tools/build.ps1` builds the runtime.

## M1 — Environment + toolchain

**Goal:** every required tool is installed and detected.

Tasks:

- [ ] VS 2022 Build Tools + C++ workload detected by `check-env.ps1`.
- [ ] CMake, Ninja, Git, Python detected.
- [ ] `make` installed (MSYS2) if using ModernGekko-Template.
- [ ] `wit` or `nodtool` installed for extraction.
- [ ] Dolphin installed as the oracle.
- [ ] `tools/fetch-tools.ps1` clones/builds DolRecomp successfully.
- [ ] `tools/build.ps1` builds the runtime successfully.

**Acceptance:** `check-env.ps1` exits 0; the runtime builds.

**Result:** MSVC 19.44, CMake 4.x, Ninja, Python 3.12 detected. DolRecomp built. The runtime builds.

## M2 — Disc extraction

**Goal:** your own dump is extracted and verified.

- [ ] ISO/WBFS/RVZ exists in `game/`.
- [ ] `tools/extract-disc.ps1` succeeds.
- [ ] `extracted/<id>/sys/main.dol` exists.
- [ ] SHA-256 of `main.dol` is recorded in `analysis/disc-manifest.json`.
- [ ] `*.rel` list (if any) is recorded.
- [ ] Dolphin boots the same dump.

**Acceptance:** the manifest is complete and Dolphin points at the same game version.

**Result:** USA disc `SO3EE9` extracted with `nodtool` to `extracted/SO3EE9`: 90 files,
2.14 GB. No REL modules. One RSO module (`Sofdec.rso`). Manifest written.

## M3 — CPU recompilation

**Goal:** all relevant CPU code becomes C.

- [ ] `tools/recompile.ps1` succeeds for `main.dol`.
- [ ] RELs (if any) are recompiled, or the interpreter-fallback decision is documented.
- [ ] Generated code compiles at least as objects.
- [ ] A symbol map exists for crash triage.
- [ ] Stats recorded: function count, output size, compile time.

**Acceptance:** no instruction fails to decode; generated code compiles cleanly.

**Result:** Broadway profile, 1,353,840 instructions decoded, 0 unknown, 331 chunks,
337 MB of C, 17 self-modifying-code candidates. Generated C compiles as objects with the
DolRecomp runtime headers.

## B1 — Baseline boot through wii-recomp

**Goal:** learn how far an existing, complete Wii runtime gets with this disc.

- [x] MSYS2 CLANG64 toolchain installed.
- [x] wii-recomp console builds on Windows.
- [x] The game translates with `add_game.py --no-pack`.
- [x] The console runs the disc far enough to show a frame.
- [x] Failure point: none reached — boots to 3D gameplay.

**Acceptance:** met. The game reaches the title screen, starts a new game, and renders
the opening 3D scene with audio and input working. Full write-up:
`analysis/wii-recomp-baseline.md`; screenshots: `analysis/wii-recomp-baseline/`.

**Outcome:** this is not a failure baseline. The project pivots from "write a runtime"
to "adopt wii-recomp as the runtime base and productize it". See ADR-010.

## M4 — Boot + OS

> Superseded by B1 for the shipping path: wii-recomp already implements M4–M9. These
> sections remain the checklist for verifying behavior and for any fork-level fixes.

**Goal:** the entry point runs to the main loop, even without video.

- [ ] MEM1/MEM2/MMIO memory map is correct.
- [ ] `__start` → `OSInit` → `main` trace is visible.
- [ ] The first thread is created and scheduled.
- [ ] `OSInit`/`OSReport` logs appear on the host console.
- [ ] No crash from unimplemented instructions.

**Acceptance:** boot log order matches Dolphin (init order, first thread).

## M5 — VI + black screen

**Goal:** the frame loop and present path work.

- [ ] VI registers implemented.
- [ ] Retrace interrupt works.
- [ ] XFB is read and presented (black/clear).
- [ ] Frame pacing at 60/50 Hz is reasonable.

**Acceptance:** a window opens, nothing crashes, and the frame counter advances.

## M6 — GX 2D

**Goal:** logos and 2D menus appear.

- [ ] CP FIFO parser.
- [ ] BP registers + rasterizer state.
- [ ] Basic TEV (1–2 stages).
- [ ] Texture decode: CMPR, RGB565, RGBA8, I4/I8.
- [ ] EFB clear + XFB copy.
- [ ] Text/2D sprites are correct.

**Acceptance:** developer/publisher logos and the first menu are visible with correct colors
and positions.

## M7 — GX 3D

**Goal:** the intro and the first 3D scene.

- [ ] XF transform, projection, viewport.
- [ ] Full TEV (multi-stage, alpha test, fog).
- [ ] All texture formats + TLUT.
- [ ] EFB copy-to-texture.
- [ ] Z-buffer, culling, blending.
- [ ] Water/reflection effects (if present) identified.

**Acceptance:** the 3D intro runs; no geometry is completely missing; major effects are visible.

## M8 — Input + gameplay

**Goal:** move and interact.

- [ ] WPAD report parsing (Remote + Nunchuk).
- [ ] IR pointer for menus.
- [ ] Keyboard/gamepad mapping.
- [ ] The game receives input and state changes.
- [ ] The main loop is stable for 10+ minutes.

**Acceptance:** can walk, talk to NPCs, open menus, and enter a dungeon.

## M9 — Audio

**Goal:** BGM, SFX, and voice output.

- [ ] AX mixer.
- [ ] DSP ADPCM decode.
- [ ] Audio DMA timing.
- [ ] Custom microcode: interpret/recompile/HLE (pick one).
- [ ] Correct sample rate and resampling.

**Acceptance:** audio is not chipmunked, not desynced, and has no heavy crackle.

## M10 — Save/load

**Goal:** NAND saves work.

- [ ] Virtual NAND layout.
- [ ] `/dev/fs` + file I/O.
- [ ] Save game writes a valid file.
- [ ] Load a save created by Dolphin.
- [ ] Save can be exported/imported.

**Acceptance:** a Dolphin save loads; a new save can be reloaded.

**Result (in progress):** the save container is decoded. `tools/wii-save-extract.py` wrote
each file 0x80 bytes early; it is fixed (`Bk` header at `0xF0C0`, `0x03ADF17E` check,
decrypt from header + `0x80`) and passes `--self-test`, but has not run on a real
`data.bin`. `RFF2_00` is the slot index and `RFF2_01`..`12` are slots. With the
`shift80` experiment save (`saves\pristine\shift80`) Continue is selectable, the Load list
shows slots 10-12 and slot 12 loads into gameplay (`work\playtests\shift80-*`,
`load11-*`). Remaining: navigate the Load list to pick a slot, and re-extract cleanly from
the original `data.bin`. Saves load from the game's Load menu, not the launcher. Do not mark
this complete. See [08-progress.md](08-progress.md).

## M11 — 30 minutes stable

**Goal:** the gameplay loop survives.

- [ ] No crash for 30 minutes.
- [ ] No major memory leak.
- [ ] No thread deadlock.
- [ ] Scene transitions are safe.
- [ ] Error log is minimal.

**Acceptance:** a 30-minute session without crashes or hangs.

**Result:** 2,043 s (34 min) run on 2026-11-23, process alive at the end, no crash.
`analysis/stability-30min/summary.json` records 8 screenshots, 10.3 MB of frame log,
6 unsupported-graphics warnings (all non-fatal), and 260 MB of captured audio. Frame
rate averaged 27.4 fps over the whole run (min 0.3 during loads, max 29.7); gameplay
slow frames had a 33.5 ms median and 51.4 ms p95, with the worst 3,367 ms spike at the
opening load. The last five minutes averaged 25.4 fps (the final minute 23.4, 17.3–27.5).
This passes the "no crash or hang" bar; the frame-rate tail and loading spikes are tracked separately (ADR-007
keeps the 30 fps lock until the timestep, animation and audio are audited).

## M12 — Full playthrough

**Goal:** finish the game.

- [ ] All main dungeons.
- [ ] All cutscenes.
- [ ] All menus.
- [ ] Ending.
- [ ] No soft-locks.

**Acceptance:** one full playthrough.

## M13 — Polish + launcher

- [x] Internal resolution scaling (`WII_RECOMP_SCALE`, launcher presets).
- [x] FSR 1.0 upscaling with RCAS sharpness (runs in `gx_vulkan.c`).
- [x] Pre-baked `.wrd` with the disc, the title library and the shader recipes.
- [x] Modern dark launcher with persistent graphics settings.
- [ ] Widescreen.
- [x] Controller remap (keyboard, gamepad buttons, deadzone, sensitivity, controls editor).
- [ ] Launcher: user supplies the ISO, builds locally, update channel.
- [ ] Crash reporter.

**Acceptance:** the build can be shared as source + launcher, with no game files.

**Result (partial):** the launcher builds and launches the game with render scale,
window size, present mode, fullscreen, FSR and sharpness. Home, Settings, Game files and
Build are separate pages; Build runs the packer as a hidden child, tails
`logs/launcher-build.log` and maps its section markers to a progress bar. The controls
editor writes `controllers.ini` (keyboard, gamepad buttons, deadzone, sensitivity), and the
input-test window proves the mapping. The packed image boots on its own. Widescreen, the update
channel and a crash reporter are still open. DLSS is not shipped: the
runtime starts NVIDIA's NGX core on the Vulkan device, creates a 1280x720 to 1920x1080
feature and evaluates one synthetic frame whose written pixels prove the library runs, but
the game feeds it nothing — motion vectors, jitter and a native 16:9 render are still
missing. FSR 1.0 is the implemented upscaler. See ADR-012.
