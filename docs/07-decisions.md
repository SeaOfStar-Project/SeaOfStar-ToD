# 07 — Decisions

Architecture decision records. Keep them short; update the status instead of rewriting
history.

## ADR-001 — Target the Wii version, not PS3

**Status:** accepted

The Wii version runs on a single Broadway core with a documented fixed-function GPU. The PS3
version needs Cell SPE recompilation and a much larger SDK HLE. Static recompilation of the
PS3 build is out of scope; RPCS3 remains the practical way to play it.

## ADR-002 — DolRecomp as the CPU recompiler

**Status:** accepted

DolRecomp handles DOL, REL, and RSO-adjacent module workflows, emits C or LLVM, and reports
unknown instructions instead of guessing. It recompiled this build with zero unknown
instructions. It is CPU-only by design, which keeps the runtime architecture ours.

Alternatives: wii-recomp's translator (full runtime, but less control), NWiiRecomp (C++
runtime, OpenGL), gcrecomp (GameCube-first).

## ADR-003 — Custom runtime in C++20, Vulkan renderer

**Status:** accepted

The runtime owns memory, OS, IOS, input, audio, and GX. Vulkan 1.3 is the primary renderer.
D3D11/OpenGL can come later as fallbacks.

Reusing Dolphin's VideoCommon is allowed and encouraged for TEV/texture work, with GPL
compliance.

## ADR-004 — GPL-3.0-or-later

**Status:** accepted

The runtime links GPL-licensed recompiler/runtime code. GPL-3.0-or-later is the simplest
compatible choice. Do not add non-GPL-compatible dependencies.

## ADR-005 — Title ID `SO3EE9`

**Status:** accepted

The USA disc header reports `SO3EE9`. Earlier assumptions of `SO3E01` were wrong. PAL and
Japanese IDs must be read from their own disc headers; do not guess.

## ADR-006 — CRI audio/video strategy

**Status:** accepted

The disc contains:

- `files/Sofdec.rso` — CRI Sofdec video module (RSO, loaded at runtime);
- `files/Sound/stream/*.cpk` — CRI CPK audio archives (ADX/HCA);
- `files/Movie/*.sfd` — Sofdec video streams.

Plan:

1. Audio: CRI decodes on the CPU; implement AX mixer + AI DMA and let the game's own CRI code
   run. If a custom DSP ucode appears, add an interpreter or recompiler.
2. Video: stub `Sofdec.rso` playback at first so the game can boot past logos. Implement
   Sofdec later, or replace FMV with a skip. Do not block boot on video.

## ADR-007 - Keep RF:ToD at its 30 fps cadence

**Status:** accepted, audited (2026-11-23)

RF:ToD advances its logic once per presented frame and waits for one VI field at the end of
the frame (`f_801b363c` -> `f_801b3394` -> `f_80292170`). The only frame-delta it computes
(`-21344`) is never read, so nothing in the loop compensates for elapsed time. On this
machine the frame work is about 21-24 ms of CPU plus graphics, longer than one 60 Hz field,
so every frame lands on the second field and the game runs at 30 fps. Removing the runtime
frame hold, spinning instead of sleeping, forcing the retrace interval to 1, or running GX
inline all leave it at ~29.5 fps (`analysis/frame-pacing.md`). A 60 fps switch would either
change nothing or run the game at double speed; it is not shipped. Any future performance
work must keep the 30 Hz cadence or add proper interpolation/delta-time support first.

## ADR-008 — No game data in the repository

**Status:** accepted

`game/`, `extracted/`, `generated/`, `build/`, `work/`, `analysis/`, and saves are gitignored.
The launcher builds from the user's own disc. See `docs/06-legal.md`.

## ADR-009 — Bring-up baseline with wii-recomp

**Status:** accepted, succeeded

wii-recomp already implements GX/Vulkan, AX, IOS, and WPAD. Building it on Windows and
running RF:ToD gave a real boot baseline and reference behavior. It boots through the
safety screen, logos, the Sofdec opening movie (with audio), the title screen, and a New
Game load into a 3D gameplay scene. Six non-fatal GX warnings; no crash. See
`analysis/wii-recomp-baseline.md`.

## ADR-010 — wii-recomp becomes the runtime base

**Status:** accepted

Because the baseline reaches gameplay, writing a second runtime from scratch (ADR-003)
is no longer justified. The shipping path is:

1. Pin a wii-recomp commit and vendor/fork it for this project.
2. Keep DolRecomp for analysis, instrumentation, and the SMC map.
3. Spend the remaining effort on the launcher, config, save path, controller mapping,
   stability, and RF:ToD-specific fixes.

The custom-runtime work in ADR-003 is kept as a fallback only if a blocking defect
appears in wii-recomp. The GPL-3.0-or-later decision (ADR-004) still holds.

## ADR-011 — FSR 1.0 in the runtime, not the launcher

**Status:** accepted, working

Upscaling belongs where the finished EFB picture is already in hand. EASU and RCAS run as
two full-screen passes inside `gx_vulkan.c` and draw into the window's picture; the game
code and launcher are untouched. The 32-bit EASU and RCAS paths from `ffx_fsr1.h` (MIT)
are ported to GLSL, and the constants keep AMD's layout: float bit patterns in `uvec4`
fields, read with `uintBitsToFloat` in the shader. Reading them with `vec2()` is the bug
that produced a black window on 2026-11-23; `analysis/fsr.md` has the bisect and the fix.

FSR only upscales. When the window would be smaller than the picture the runtime keeps
the linear blit. At 1:1 the passes still run, so FSR acts as RCAS sharpening. The option
is environment-driven (`WII_RECOMP_FSR`, `WII_RECOMP_FSR_SHARPNESS`) and the launcher
writes those variables from `config/launcher.ini`.

## ADR-012 - DLSS is not shipped yet; the GPU is not the blocker

**Status:** accepted

The GPU supports DLSS Super Resolution. On the test system the GPU
reports it through NGX. A DLSS integration needs the driver's `_nvngx.dll`, NVIDIA's own
`nvngx_dlss.dll`, a lower-resolution scene, depth, camera motion vectors and a jittered
projection. "The device does not support DLSS" would be wrong.

What RF:ToD lacks is the integration in wii-recomp:

- the NGX core now starts on the Vulkan device, creates a 1280x720 to 1920x1080 feature
  and evaluates one synthetic frame as a local experiment (`WII_RECOMP_NGX=1`,
  ADR-012 update below), but the game never feeds it a frame yet;
- no motion vectors and no jittered projection. GX provides a depth picture, but DLSS
  also needs per-pixel motion and a sub-pixel offset every frame;
- the picture is anamorphic 4:3 (960x720) stretched into a 16:9 window, while DLSS
  expects square-pixel input and output of the same aspect ratio. A native 16:9 render
  (the still-open widescreen work) would come first;
- the UI has to stay out of the upscaled scene.

Licensing is a separate constraint: NGX/DLSS is proprietary and this project is
GPL-3.0-or-later. A local experiment is one thing; shipping the DLL and an integration
is another and needs NVIDIA's terms and a project ID. The launcher therefore has no DLSS
toggle, and no fallback pretends to be one. A future DLSS path would be a separate render
path feeding the same swap image, with FSR 1.0 as the fallback.

**Update (2026-11-23):** the experiment reached the full feature lifecycle. With
`WII_RECOMP_NGX=1` the runtime loads the driver's `_nvngx.dll`, enables the instance and
device extensions NGX asks for, starts NGX on the device, and the capability parameters
report DLSS Super Resolution as available. It then creates a 1280x720 to 1920x1080
feature, evaluates one synthetic frame, and releases the feature. The evaluation writes
the whole 1920x1080 output: cleared to magenta before the call, the readback reports
`2073600 of 2073600` pixels changed, with the input's mid-gray at the first pixel. That
is a real DLSS-executed picture, not a fallback: the core's log shows it built the
network (85.7 MB of history, weights and scratch) and ran with `NeedHistoryReset 1`.
Evaluation needed a private parameter map because the core's own map refused the
resource lookup during `EvaluateFeature` even though the same pointer round-tripped
through its getter; details in `analysis/fsr.md`.

The feature library (`nvngx_dlss.dll`) is read from the console's folder or
`WII_RECOMP_NGX_FEATURE_PATH` and stays outside this tree; nothing NVIDIA ships is part
of the project. What remains is real input: motion vectors, a jittered projection, a
native 16:9 render and UI exclusion. Until then the game still uses the linear blit or
FSR, and the launcher has no DLSS toggle.

## ADR-013 - Controller buttons and sticks are remapped in the runtime

**Status:** accepted (2026-11-23)

The launcher lists controllers, but gameplay input is sampled in the runtime every
frame, so the mapping has to live there. `controllers.ini` gains a `[pad]` section: each
line names a Wii button and the host button that presses it. A binding replaces every
source of that Wii button in every layout (Remote+Nunchuk, Classic and sideways), with
`auto` restoring the layout and `none` leaving it unused. Sticks get a radial deadzone
and a speed applied once in `pads.c` before the layout sees them, so the Nunchuk stick,
the Classic sticks and the pointer stick share one shape.

The launcher's input check reads the same deadzone and speed from the file and draws the
raw and shaped positions; it reads the pad through SDL3 directly and changes no game
state. The mapping and shaping live in `pad_map.c`, so a small C harness can test them
without a controller.

The launcher's **Controls** window edits the same file rather than introducing a second
settings store. It writes active overrides with `WritePrivateProfileStringW`, reads the
runtime's defaults from its own copy of the tables, and leaves the commented guidance in
place. The runtime stays the authority: anything the editor writes is parsed exactly as a
hand-edited line would be, and an invalid hand-edited value still stops the console with
its line number.

Alternatives: remapping per layout only (the same button would still mean different
things in sideways and Classic), remapping in the launcher (gameplay input never passes
through it), and per-axis deadzones (a radial circle is the usual gamepad behaviour and
is easier to explain and draw).

## ADR-014 - The pre-baked image carries the disc, the title and its recipes

**Status:** accepted (2026-11-23)

The Launcher's **Rebuild title** previously left three pieces around the build: the
extracted folder, `title.dll` and a `shaders.bin` beside the console. That works only if
all three stay together, and it makes "pre-baked" a property of the folder rather than of
the build.

`add_game.py` now packs one `.wrd` after the translate/compile step. Its table holds the
disc pieces at their original offsets, the `windows-x86_64` native library and the shader
recipe file; the console already knew how to read the first two and `drive_shaders()`
reads the third. Booting the image with no loose library or title folder extracts the
library once into `native-cache\` under a hash of its bytes, loads the recipes from the
image, and writes them to the title folder the console owns. The image is therefore
self-contained: `wii-recomp.exe` plus `SO3EE9.wrd` boots to the title screen.

Play prefers the image when it is at least as new as `title.dll` and the recorded
recipes. Recipes grow when the game draws a scene the set does not cover, which happens
during play, not during the build; the image is refreshed by the next rebuild. The
launcher names that state: **PRE-BAKED** when the image is current, **READY** when play
will use the extracted folder.

Alternatives: keep the folder as the unit (needs three paths to stay in sync and cannot
be copied as one artifact), and pack only the disc (leaves the library and recipes loose,
which is what the earlier image did). The image contains game data and generated code, so
it stays local like the extracted tree; nothing about this changes the distribution
rules.

## ADR-015 - Launcher pages follow the reference recompilation launchers

**Status:** accepted (2026-11-24)

The launcher had grown into one 1000x700 window that stacked the hero, six actions, the
controller list and the graphics card, with the title rebuild spawning a separate
PowerShell console. It worked, but it did not match the task-first home, step-by-step
setup and left-navigation settings page common to other native-port launchers, and the
rebuild left the launcher entirely.

The window now has four pages behind one header:

- **Home** keeps the task first: the primary button is Play when the build is ready and
  Set up otherwise, and the summary card carries the build facts that used to be five
  separate settings. Game files, Rebuild, Controls, Saves, Logs and Open config stay on
  the home page as secondary actions.
- **Settings** opens from the gear and groups Display, Graphics and In-game; this is the
  only page that writes option values.
- **Game files** is the first-run destination when Play has nothing to run. It shows the
  four paths the build reads and puts the picker beside each one.
- **Build** starts `tools\wii-recomp-build.ps1` as a hidden child process with its output
  redirected to `logs\launcher-build.log`, tails the file on a 300 ms timer, and maps the
  script's `== section` markers to a progress bar. A Job object with
  `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` ties the build to the launcher; closing the window
  asks first and stops the build. Navigating back leaves it running: the header pill says
  BUILDING until the exit code arrives.

Alternatives: keep everything on one page (simplest, but the settings and file paths get
buried and the window has no room for the log); spawn PowerShell visibly as before (the
build owns a console window and the launcher cannot report progress or stop it cleanly);
embed the build script as a library (couples the launcher to Python/CMake internals that
change).

The rest of the launcher is unchanged: the same palette, hand-painted controls, DPI
scaling, controller polling and `controllers.ini` editor. The only new dependency is
`comdlg32`/`ole32` for the file pickers, and the shell's drop handler for dragged paths.

## ADR-016 - Android port first, iOS later via cloud build

**Status:** accepted (2026-11-26)

The project runs natively on Windows x64. A phone port was asked for, with the Samsung Galaxy
M33 5G (Exynos 1280, Mali-G68 MP4, Vulkan 1.1) as the first target. The runtime fork already
builds for 64-bit Arm Linux (Raspberry Pi 5), the generated C has no x86 intrinsics, and the NDK
Clang supports the `musttail` calls the generated code uses, so Android is the cheaper first
mobile target. iOS needs a Mac, a signing identity and Apple's rules on loading code; the
Windows PC alone cannot build or sign it.

Decision:

- Android arm64-v8a first, from the same repository (new top-level `android` folder) and the
  same generated sources as Windows.
- SDL3 provides window, input, audio and the Vulkan surface on Android, through a small platform
  layer (`window_sdl.c`, `audio_output_sdl.c`, `host_input.c`).
- The runtime is built as `libwiirecomp.so` and the title as `libtitle.so` on the user's PC from
  the user's own dump (`tools\android-build.ps1`) and pushed with adb (`tools\android-push.ps1`);
  the image is read from app-specific storage. Nothing derived from the game is committed or
  distributed (ADR-008), so the repository holds no game data. The app icon and label from the
  user's own disc are made locally (`tools\android-branding.py`) and never tracked; the tracked
  defaults are a neutral icon and the label SeaOfStar-ToD.
- The game runs in its own process (`:game`); leaving it ends the process.
- Every capability is gated by evidence (milestones A0-A5 in `docs/android-port-plan.md`), and
  30 fps is reported honestly, as on PC (ADR-007). No fake modes.
- iOS is deferred and not started. Static recompilation needs no JIT, which is why iOS is
  plausible, but a title library the user builds cannot be loaded into a signed app, so an iOS
  build would have to link the title statically and be built and signed per user by a Mac or a
  cloud build service (Codemagic is the candidate). That is a later ADR. [hypothesis]

Alternatives: iOS first (needs Mac/signing and a static-link title mode before anything runs);
a single-library Android build with the title linked into the runtime (simplest loading, but
every rebuild of the title relinks and repackages the whole app); NativeActivity without SDL
(less Java, but loses SDL's controller, sensor and audio code the runtime already uses);
an Android emulator test of the Windows build (proves nothing about the phone's GPU or CPU).

Consequences: the runtime gains target-independent platform interfaces (window, audio, network,
remotes) and an explicit cross-build target; the Android shell, builder flow and touch UI are
new components to maintain.

## ADR-017 - No dual-source blending on Android: one pass where provably equal, else a second alpha pass

**Status:** accepted (2026-11-26)

GX blending reads the stages' alpha through the SOURCE1 factors while the alpha channel stores a
different alpha. On PC this is dual-source blending. The phone's Vulkan driver does not expose
`dualSrcBlend`, so the runtime needs another way to get both alphas through one colour output.

Decision: `gx_alpha_split.h` decides per draw (a pure function of the render state, unit-tested
in `test/c/test_alpha_split.c`):

- Draw once, outputting the stages' alpha, when the result is exactly the same number as the
  dual-source formulation: colour not written and no alpha factor reads it, or the source alpha
  factor is ZERO (the product is exactly 0 in UNORM8 whatever the stored alpha is), or a constant
  alpha of 0.
- Otherwise draw twice: the normal draw, then an alpha-only draw with the alpha to store. There
  is no exact single-pass form for a constant other than 0.
- `WII_RECOMP_GX_DEBUG=fullalphapass` restores the earlier rule (a second pass wherever a
  SOURCE1 factor is read) for comparison.

Measured on the Galaxy M33 in town, throttled (CPU capped at 1.54 GHz, GPU 611 MHz): the median
frame was 50 ms with the earlier rule and 33.3-33.4 ms after (p95 35.8-37.9 ms, no frame over
50 ms). In that scene draws per frame fell from 7311 to 6733, second alpha passes from 6733 to
0, and pipeline binds from 13506 to 215. Every single-pass case above is bit exact, not an
approximation.

Alternatives: always two passes (correct, but measured too slow when throttled); using the
stage alpha as the stored alpha everywhere (wrong alpha in the framebuffer, which later draws
read).

## ADR-018 - Android thread placement: the game thread and the commands thread get the fastest cores

**Status:** accepted (2026-11-26)

The Exynos 1280 has two Cortex-A78 and six Cortex-A55 cores. The recompiled game runs on one
guest thread and a second thread feeds the GPU command ring; both are latency-bound, and the
scheduler was free to put either on a slow core.

Decision (`host_perf.c`, Android only, `WII_RECOMP_AFFINITY`):

- auto (default): the game thread and the commands thread go to the phone's fastest cores,
  read from sysfs by maximum frequency; with exactly two fastest cores each thread gets one of
  them (measured best on the M33); other threads are left to the scheduler. A phone whose cores
  are all alike, or that will not say, is left alone.
- `off`, `split` or an explicit list (`6,7`) are available for measurement.
- A performance-hint session with a 33.3 ms target tells the system what the frame needs
  (`WII_RECOMP_PERF_HINT=off` disables it).
- The runtime's threads are named (`host_thread.h`), so profiles say which thread is which. A
  busy-poll in the SDL UI thread, which took 34% of the samples in a profile, was removed (below
  1% after).

Alternatives: leave placement to the scheduler; pin everything to the fast cores (they would
then share two cores with the audio and UI threads).
