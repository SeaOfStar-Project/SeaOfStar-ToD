# 08 — Progress snapshot

Date: 2026-11-24 (evening update). This page answers "where is the project now" without reading the whole
history. It is a snapshot, not a plan; the milestones remain the acceptance record.

## Shipping path

A Windows x64 build of the wii-recomp runtime (ADR-010), the user's own `SO3EE9` disc
translated to C, and a native Win32 launcher. The playable unit is the packed
`games/SO3EE9.wrd`: the disc at its original offsets, the compiled title library, and the
shader recipe file. Play uses that image while it is current; otherwise it runs the
extracted tree with `WII_RECOMP_NATIVE`.

## Working and verified

| Area | State | Evidence |
|---|---|---|
| Boot | Safety screen → logos → opening movie → title → New Game → 3D gameplay | `analysis/wii-recomp-baseline.md` |
| Stability | 34 minutes, process alive, no crash | `analysis/stability-30min/summary.json` |
| Frame rate | 30 fps lock, frame-coupled; launcher Play, image boot: median 33.3 ms, p95 34.5 ms, ~7 frames >50 ms per minute; scripted playtests at the runtime's default scale 2 drop about one frame a second (GPU load), at the launcher's 1.5 they match play | `analysis/frame-pacing.md` |
| Packed image | ~2.36 GB, 91 entries, boots image-only; rebuilt 19:56:47 with 341 recipes and 14186 profiled functions | `analysis/prebake.md` |
| Shader pre-bake | `WRSR` v2, 807-byte records, 341 recipes in the image (2026-11-24; collected by manual play); `-Verify` boot "341 from the disc" (subagent report) | `logs/boot1.err.log`, `logs/tl5.err.log` |
| Upscaling | Real FSR 1.0 (EASU + RCAS) in `gx_vulkan.c`; render scale, present mode, fullscreen | `analysis/fsr.md` |
| Launcher | Home, Settings, Game files, Build pages; in-window build log and progress | `launcher/`, `docs/04-tooling.md` |
| Controls | Keyboard remap (4 keys per action), per-button gamepad remap, radial deadzone and sensitivity, input-test window, controls editor writing `controllers.ini` | `docs/controls.md` |
| Saves | Container decoded; extractor fixed and run on the original save; Continue works, slot 12 loads into gameplay (`realsave2-*`) | `tools/wii-save-extract.py`, `work\playtests\shift80-*`, `work\playtests\load11-*` |
| Scripted input | Control-pipe verbs (`press`, `hold`, `release`, `stick` for classic and Nunchuk, `pointer`), ms timeline, game-side screenshots | `logs/play-script.ps1`, `logs/timeline-run.ps1` |
| Symbols | 35,717 title entries, 1,451 console entries | `analysis/symbols-title.csv`, `analysis/symbols-console.csv` |
| Reverse engineering | REA 6.10.1 with Ghidra 12.2 (needs JDK 25) | local setup |
| Playtests | `tools\playtest.ps1`: locked, one run at a time, per-run save folder, private `title-data` unless `-Coverage`, `summary.json` | `docs/04-tooling.md` |

## Verified 2026-11-24 (evening)

Proven:

- **Load list navigation.** The pipe verb `stick x y` drives both the classic and the Nunchuk
  stick (`input.c` `pipe_stick`, logs `input stick p1 x y`). The Load list reads the Nunchuk
  stick, which is why `press` up/down and the pointer did nothing. With `stick 0 1` the cursor
  moves one slot per pulse. Slot 11 (Aden, Year 2 Spring 21) and slot 12 load into gameplay.
  In the 12-slot tour, screenshots of slots 1, 3, 5, 7, 9 were inspected: the cursor sat on the
  intended slot, and 3, 5, 7, 9 reached gameplay with HUD; slot 1 showed no HUD. Even slots
  2-10 were not inspected; slot 12's tour run was stopped early.
- **Recipe collection.** 110 -> 157 after one scripted run from slot 11; the same route again
  added 0; the 12-slot tour added 1 (158) because every save starts indoors; 3.5 minutes of
  manual play through the launcher added 183 (158 -> 341). Manual play is the effective way.
- **Image.** `SO3EE9.wrd` rebuilt 19:56:47 with 341 recipes, 14186 profiled functions, 2.36 GB
  ("SO3EE9 is ready in 20 s"). `-Verify` boot: "341 ready in 19 ms (341 from the disc)"
  (subagent report). Mtimes wrd 19:56:47 > title.dll 19:56:00 > shaders.bin 19:54:09, so Play
  uses the image. Launcher Play logs go to `logs\play\<stamp>.log`.
- **Recipes do not explain the hitches.** Same scripted route: 47 new recipes gave p95 48.1 ms
  and 158 frames over 50 ms; 0 new recipes gave p95 49.9 ms and 237 frames over 50 ms. Slow
  frames are mostly "idle" and "present", not disc, audio or graphics waits.
- **User session (windowed launcher):** median 33.3 ms, p95 35.1 ms, 54 frames over 50 ms,
  max 774 ms; the user reports stable fps. Different route and mode, so not like-for-like with
  the scripted runs.
- **Launcher Play, packed image (proven):** `logs\play\20261124-204358.log`, drive = `SO3EE9.wrd`,
  341 recipes, ~327 s (idle ~60 s at the main menu, then play): p95 34.5 ms, 40 frames >50 ms
  (7.4 per minute), no new shaders. Earlier extracted-tree launcher session
  (`20261124-195055.log`, ~195 s): p95 35.1 ms, 54 frames >50 ms (16.9 per minute). Booting the
  image does not cause hitches; real play is near a steady 33.3 ms.
- **Scripted playtests (proven):** image boot, same route, windowless (`act1`) and windowed
  (`act1win`): about one 50-65 ms frame per second on every screen incl. the title, p95 ~49 ms,
  76-108 frames >50 ms per minute. `WII_RECOMP_ACTIVITY=1` shows 0 new shaders and 0 new pipelines;
  >100 ms frames fall on screen transitions. Windowed vs windowless: no difference. The 10 s
  save threads (`profile.c`, `gx_vulkan_pipeline.c`) cannot explain a 1 Hz pattern. Conclusion
  (superseded 2026-11-24): the hitch is GPU load at render scale 2, not the harness. 70 s idle at
  the title: default scale 2 82.6 frames >50 ms per minute; `WII_RECOMP_SCALE=3/2` alone 26.0; full
  launcher env (fifo, 1280x720, FSR 0.20, 3/2, windowed) 34.3; fresh vs copied shared2 82.6 vs
  81.3. The test GPU needs slightly more than 33.3 ms at scale 2, so about one frame a
  second doubles. `playtest.ps1` now reads `config\launcher.ini` by default (`-Scale`,
  `-RuntimeDefaults` to override).
- **Open tool bug:** `playtest.ps1 -Tree` closes the
  game ~2 s after boot ("the window was asked to close" before the first frame); cause unknown,
  open tool bug.

- **Frame-time follow-ups (proven):** 70 s idle runs, frames >50 ms per minute after the first 600
  frames: scale 2 102.5 (p95 50.2 ms); launcher settings with control pipe 25.7 (p95 38.2); same
  with `-NoControl` 19.1 (p95 35.7), within noise; scale 1 with launcher settings 28.3 (p95 38.8),
  so below 1.5 the scale no longer matters; live vs fresh `shared2` 81.3 vs 82.6 (no effect).
  Scripted route `cov1` after 80 s: scale 2 (`act1`) 74.2 (p95 49.1); launcher settings (`act2`)
  37.4 (p95 36.2). User launcher sessions after boot: 7.4 (p95 34.6, image, 20:43); ~60 s at the
  main menu 1.4 (p95 33.9).
- **Frame-time gap (open, not proven):** idle playtests sit on the opening movie (Sofdec) and attract
  loop, which the user skipped, so they are not like-for-like with the user's title-screen minute.
  The scripted route shows ~37 per minute vs the user's ~7 of free play; content and route differ,
  so the gap is unexplained, not proven to be the harness. Next: same route played by the user, or
  the playtest replaying a recorded input log.

## Fixed earlier in this session

- **Screenshot stalls.** The picture readback buffer was allocated device-local, so every
  `shot` copied 5 MB over PCIe and froze the frame for 1.7–3.1 s. It is now a host-cached
  read ring: handover is under 200 ms, and a shot-heavy run has one 1 s stall (the normal
  first-frame Vulkan start) instead of 32–37. Evidence: `logs/tl5.err.log`.
- **Extractor bug.** `tools/wii-save-extract.py` wrote each file 0x80 bytes early; the first
  0x80 bytes were a `data.bin` file header decrypted by mistake (XOR with
  AES-dec(SD_KEY, 0) = `506d6932773ce0203684e4b353822861` shows the file-name tail). Fixed:
  `unpack_official` finds the `Bk` header at `0xF0C0` (magic `0x426B0001`), checks
  `0x03ADF17E` on every file header and decrypts from header + `0x80`; `--self-test` with a
  synthetic `data.bin` passes. Not yet run on a real `data.bin` (the original was not found).
- **Slot model.** `RFF2_00` is the slot index; `RFF2_01`..`12` are slots, `13`..`15` empty.
  Index entries start at `0x10`; raw u32 at `0x04`/`0x0C` rises with progress; `RFF2_11`
  (Aden) is the furthest. `tools/save-info.py` prints this. Every boot rewrites all 16 files.
- **`shift80` experiment.** Stamp `saves\pristine\shift80` (= file[0x80:] + 0x80 zero bytes):
  Continue becomes selectable, the Load list shows slots 10-12 with level, date and play
  time, "Game loaded." and slot 12 (Sonja, Year 1 Winter 18) loads into gameplay at House 1F
  with HUD, ~33 ms frames, no crash. Superseded: the original save (GameFAQs, uploader ohnoe, `rune-factory-tides-of-destiny.23334.bin`, sha256 `6c6e03f17a607cae24f3b475d72bd02b30dbaa5dd4ae09c7ffc0bf40997a59a3`) was re-imported with the fixed extractor (`banner.bin` restored too). The live folder and pristine stamp `20261124-183359` hold complete slots (backup `20261124-183359` in `saves\backup`); the slot tails `shift80` had zeroed (slots 01-12) are real. `work\playtests\realsave2-*` loaded slot 12 into gameplay (House 1F); 100 s frame log: median 33.4 ms, p95 38.4 ms, one 1,052 ms frame (first Vulkan frame).
- **Boot flow.** Windowless: safety screen to ~23 s, logos, movie from ~49 s; the pointer
  changes nothing (identical frames); `press a` at 52 s skips the movie and the title menu
  waits from ~53 s. With the misaligned import the cursor cannot leave New Game (all
  directions, hold, stick, pointer, nunchuk tried), so A started New Game: that was the
  `boot1` prologue. `boot1` t119 "..." from "???" is a normal dialogue line. Evidence:
  `work\playtests\nopointer-*`, `pointer-*`, `dirs-*`, `sweep-*`, `nunchuk-*`.
- **Windowless runs.** `WII_RECOMP_NO_WINDOW=1` skips held-show, window copy, swapchain
  acquire and present. The game never waits for the display, and `shot` still returns its
  own 1280×960 picture.
- **Observability.** `hand_picture_over` logs when a handover takes over 200 ms; the control
  pipe gained `timeline`; `logs/menu-detect.py`, `logs/boot-watch.ps1` and
  `logs/load-save.ps1` automate title detection and the Load attempt.

## Shader miss cost measured (2026-11-25)

Proven (playtests `work\playtests\misscold-20261125-184058` and `misswarm-20261125-184328`; same
timeline: load a late save slot, then about 40 s of walking; launcher graphics settings;
`WII_RECOMP_ACTIVITY=1`). New runtime instrumentation logs `shader miss: kind=... shader_ms
pipeline_ms total_ms` and a `prebake: ...` summary line.

| Run | Misses | Detail |
|---|---|---|
| Cold (`WII_RECOMP_NO_SHADER_CACHE=1`, prebake skipped) | 71 | new-program n=42: median 8.0 ms, p90 18.5, max 127.5, sum 539 ms (shader build median 1.4 ms, `vkCreateGraphicsPipelines` median 6.7 ms). state-only n=29: median 0.0 ms, p90 6.0, max 10.8, sum 43 ms. |
| Warm (normal; this PC's local caches warm: `stages.bin`/`pipelines.bin`, 700 pipelines) | 0 on the route | prebake recipes=373 programs=205 pipelines=700 in 20 ms; shader cache 293 stages / 700 pipelines loaded in 34 ms. |
| Fresh cache (`WII_RECOMP_SHADER_CACHE` on a new empty folder, recipes on; `missfresh-20261125-184805`) | 0 on the route | `prebake: recipes=373 programs=205 pipelines=373 total_ms=212`: first-boot cost of building every recipe from the image, one-off at boot. |

- Frames over 50 ms: cold 167, warm 154, fresh 180 (over 100 ms, cold vs warm: 8 vs 2). Proven: the count is nearly the
  same with and without shader work, so on this route the >50 ms hitches are not explained by
  shader misses. The counts include boot and loading.
- Hypothesis: the slow-frame breakdown shows them outside stream, present, disc, audio and idle
  (stream 0, present ~0.1 ms, idle a few ms), which would point at guest CPU or draw submission.
  Not proven. This is the already-recorded unexplained scripted-route gap (Goal 2), not a new
  finding.

Caveats (hypothesis and limits): NVIDIA's driver shader disk cache was not bypassed (the
`__GL_SHADER_DISK_CACHE_PATH` folder was never created), so the cold and fresh timings are lower
bounds for a new machine. The route stays inside the starting house/workshop (screens from all three
runs show the same scenes and the character moving), so coverage of outdoor areas is untested.
State-only cost was measured on one NVIDIA GPU only. The near-zero state-only cost may
reflect the warm driver cache.

Conclusion: with the prebaked image a covered route has zero misses, both with warm local caches and with a
fresh empty cache (212 ms one-off boot cost). The image prebake alone covers this route. State-only pipelines cost
about 0 here, so dynamic state is not needed for stutter on this PC (recommendation; the user decides). Next
priorities: (a) a map-sweep bot for coverage of new programs outdoors, since a new-program miss costs about 8 ms median and up to about 128 ms; and (b) the unexplained scripted-route hitch gap.

Runtime change (uncommitted, branch `seaofstar-tod`): activity-gated miss logging and the prebake
summary line.

## Map sweep shader coverage (2026-11-25)

Goal 4 (recipe coverage). Facts first, hypotheses marked.

**Facts: tooling and reverse engineering**

- Runtime (uncommitted, branch `seaofstar-tod`): control-pipe verbs `poke` and `call` plus a reply channel (`reply on`), an activity miss line that carries the recipe hash, and `tools\recipe-hashes.py` (prints the runtime's recipe hash for every record of a `shaders.bin`).
- RE (REA, SO3EE9 USA): map manager at 0x805F9138; current map is `*(0x805F9144)+0xAC`; request words at +0x7E0 (new id), +0x7E4 (previous id), +0x7E8 (0xffffffff); post function 0x8012E234; getter 0x8000EE60. 265 valid IDs. Notes in `analysis/map-warp-notes.md` (gitignored).
- First warp to ID 0 landed in Fenith Port, confirmed by a shot and by the getter.
- `tools\map-sweep.py` generates the timelines (load slot 11, then warp through the IDs, 12.5 s per map).

**Facts: sweep results**

- Cold sweep of the 94 base maps (4 runs plus 1 repeat): 93 warped; ID 186 faults in guest code on load (skipped); 4xx/5xx IDs do not warp (progress variants).
- 510 distinct recipes seen. 312 of the 373 manual recipes (84%) were rediscovered; 198 were new.
- Coverage runs cov0-3 grew the shared `shaders.bin` from 373 to 550 (backup `shaders.bin.bak-20261125-pre-sweep`).
- Image rebuilt with 550 recipes; `title.dll` recompiled too (profiled functions 15187). Setup state is current.
- Verification with a fresh shader-cache folder on maps 0-33: prebake 550 recipes / 279 programs / 550 pipelines in 251 ms; 6 misses on the route (2 new-program, 4 state-only), against about 295 recipes that would need compiling cold.

**Hypotheses (not proven)**

- The 61 manual recipes the sweep did not rediscover come from battles, effects, menus and events.
- The 6 residual misses vary with time of day and NPCs.

**Next**

- Repeat the sweep under other season, time, weather and progress-flag conditions; add battle content.
- Saturation criterion: a full sweep under new conditions adds 0 recipes.

## World-state and battle sweeps, 617 recipes (2026-11-26)

Goal 4 (recipe coverage). Facts first, hypotheses marked. Run folders are under `work\playtests`.

**Facts: reverse engineering** (`analysis\world-state-notes.md`, `analysis\battle-notes.md`, both gitignored)

- Calendar object: `P = *(0x805EAA48)`. Season, week, day, weekday and hour bytes were validated live; weather is at `P+0x64`; the clock-speed word at `0x805EAA94` freezes time when set.
- Festival flags 0x346..0x356 are derived from the date and have no storage. Setting the date triggers a festival; poking the flag does not.
- The flag setter only works for flags that have set scripts (event-flag attempt added 1 recipe; flags are effectively read-only).
- The direct monster factory at 0x80075e0c blocks (it waits on a load), so calling it through the pipe is unsafe. Natural spawns in dungeon maps are used instead; the dungeon maps with natural enemies are listed in `battle-notes.md`.
- Festival event ids were confirmed by `call 8021ae40`.
- Harness additions: timeline variables (`-> NAME`, `$NAME+off`), `call` with r3..r10, `pokef`.

**Facts: sweeps** (coverage runs, or `-Sweep -Parallel` merged with `tools\merge-recipes.py`)

| Step | Recipes after |
|---|---|
| Start (2026-11-25 map sweep) | 550 |
| Seasons spring/summer/winter/night at 10:00 and 20:00 over 93 maps | 557, 567, 572, 580 |
| Weather 1-4 on 20 outdoor maps | 582 |
| Dusk 17:00 | 593 |
| Event-flag attempt | +1 |
| Festival dates: 17 festivals x maps 0-9 | +12 |
| Dungeon battle sweep, 40 maps with attacks (magic and damage numbers visible in shots) | +12 |
| Merged total | 617 |

- Profile: 15540 functions. Image rebuilt with 617 recipes; setup state current.
- Parallel: 3 windowless instances ran fine on this PC (CPU ~91%, GPU 57%, VRAM 1.5 of 6 GB, free RAM ~10.7 GB), about 3x faster. Frame times are not taken from parallel runs.
- Verification with fresh shader-cache folders, 3 parallel runs: prebake 617 recipes / 296 programs / 617 pipelines in 314-381 ms (under parallel load). Misses: maps (spring, 24 maps) 4 (max 1.49 ms); festival (57 warps) 1 (1.98 ms); battle (13 dungeons) 0.

**Facts: tooling.** Ghidra-GameCube-Loader built from master for Ghidra 12.2 (a local `tools/wii-re` folder outside the repository); decomp-toolkit 1.9.1 named 358 SDK functions. REA opens the ELF wrapper, but its Windows Ghidra decompiler rejects non-PE.

**Hypotheses (not proven)**

- The remaining recipes come from unseen monster types, spells, weapons, cutscenes and menus.
- The total is likely hundreds to about 1000, not tens of thousands (fixed-function GX).

**Next:** more content (other monsters, spells, weapons, cutscenes, menus); saturation criterion unchanged (a full sweep under new conditions adds 0 recipes).

## Android port, Galaxy M33 5G (2026-11-26)

Samsung Galaxy M33 5G (Exynos 1280, Mali-G68 MP4), arm64-v8a, from the user's own dump (ADR-016 to
ADR-018, `docs/android-port-plan.md`). Measured on the device with the runtime's own frame summary
(`frames: n, median, p95, max, over 50 ms`, every 10 s):

Proven:

- Boots to gameplay from the packed image with the android-arm64 title library; a late save slot loads.
- Indoors (room): median 33.2-33.5 ms, p95 about 36 ms, 0 frames over 50 ms.
- Town, cold start: median 33.3-33.5 ms.
- Town, throttled (CPU capped at 1.54 GHz, GPU 611 MHz): median 50 ms with the earlier alpha rule; after
  the alpha-pass change (ADR-017) 33.3-33.4 ms, p95 35.8-37.9 ms, 0 frames over 50 ms per 10 s after warm-up.
- Draws per frame in town: 7311 with 6733 second alpha passes before; 6733 with none after. Pipeline binds
  per frame 13506 before, 215 after.
- The SDL UI thread busy-poll is fixed: it was 34% of the profile samples, below 1% now (ADR-018).
- Shader pre-bake: 617 recipes compile on the device in about 10-12.6 s.
- Touch controls with a layout editor, and a launcher with settings (FPS overlay, FSR), work on the device.

Not proven:

- A physical controller on the device (the mapping is written and shared with the PC code; no pad was connected).
- Sessions longer than 15 minutes, and thermal behaviour over them.
- iOS: planned later via a cloud build, not started.

Open: an in-app builder flow with progress, update channel and crash reporter (A5); a Release (non-debug)
build and signing; checking PAL and Japanese discs.

## Open

| Item | State |
|---|---|
| Remaining slots | Slots 2, 4, 6, 8, 10 not inspected visually; slot 1 shows no HUD (hypothesis: early scripted scene). |
| Packed image freshness | Any Play that records a new recipe makes `shaders.bin` newer than the image; Play then falls back to the extracted tree. Goal 4 needs automatic rebuild/merge in the launcher, or an image check that tolerates a newer `shaders.bin`. |
| Launcher "rebuild from dump" | Button untested. |
| Async shaders | Deferred until a measurement shows shader compiles cause stalls (current evidence: they do not). |
| Hitches (200-800 ms spikes) | Goal 2 CPU/present work, to be profiled. |
| Playtest frame times | Explained: scale 2 GPU load, not the harness; playtest uses the launcher's graphics settings by default. Open: fix `playtest.ps1 -Tree`. |
| Recipe list in the repo | To check against ADR-008 (GX state and hashes only, no game code or assets). |
| Widescreen | Not implemented. |
| Crash reporter, update channel | Not implemented. |
| Physical gamepad | No device connected; remap was tested with the virtual pad. |
| DLSS | Experiment only. Needs motion vectors, jitter, native 16:9, UI exclusion and a licence resolution; not shipped. |
| 60 fps | Closed. The loop is frame-coupled; a real 60 fps mode needs engine-level interpolation and a large CPU/GX speedup. |

Saves are loaded from the game's own Load menu by design, not from the launcher, so a
launcher save picker is not an open item.

## Next steps

1. Make the image survive new recipes and test "rebuild from dump" (goal 4).
2. Profile the 200-800 ms spikes (goal 2).
3. Then the remaining goals: UI flow and features, input remapping, and the stretch goals.

## Evidence index

- Local evidence (not published): `analysis/frame-pacing.md`, `analysis/fsr.md`, `analysis/prebake.md`
- `analysis/wii-recomp-baseline.md`, `analysis/stability-30min/summary.json`
- `work\playtests\` 12-slot tour and `cov1`/`cov2` runs, `logs\play\`, `work\playtests\realsave2-*`, `shift80-*`, `load11-*`, `nopointer-*`, `pointer-*`, `dirs-*`, `sweep-*`, `nunchuk-*`
- `analysis/boot1/` (game pictures every 5 s; this run started New Game)
- `logs/tl5.err.log` (shot fix), `logs/boot1.err.log` (recipes and boot)
- `docs/controls.md`, `docs/07-decisions.md` (ADR-014 packed image, ADR-015 Build page)
