# launcher (SeaOfStar-ToD.exe)

Native Win32 front end for the wii-recomp build. It starts the console with the user's
own extracted disc or packed image, hides the environment plumbing (MSYS2 DLL path,
`WII_RECOMP_NATIVE`), and exposes the graphics and input settings without editing files by
hand.

The window is drawn by hand with GDI+ (rounded cards, gradient buttons, switches and a
slider) so it matches the game's dark look without a UI toolkit. The title bar uses the
Windows 10/11 dark-frame attribute.

## Build

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\build-launcher.ps1
# -> build\launcher\SeaOfStar-ToD.exe
```

Requires Visual Studio 2022 Build Tools (Desktop C++) and CMake. Links only system
libraries (`gdiplus`, `dwmapi`, `shell32`, `user32`, `comdlg32`, `ole32`).

## Pages

The window follows the common layout of native-port launchers: one header, a
task-first home, and everything else behind it.

- **Home** is the task. The primary button is Play when `Ready()` and Set up otherwise
  (which opens Game files); a ghost Game files button sits beside it. The controller card
  lists SDL3 gamepads with Test input and Rescan, an action row holds Game files, Rebuild,
  Controls, Saves, Logs and Open config, and a summary card reports the packed image, the
  shader recipe and profiled-function counts, the display shape and 30 fps. The gear opens
  Settings.
- **Settings** groups the options in three cards:
  - DISPLAY — fullscreen, window size, present mode;
  - GRAPHICS — render scale, FSR 1.0, sharpness;
  - IN-GAME — frame rate overlay, Test input, Controls.
  Every change is written to `config/launcher.ini` immediately and applies on the next
  start. Back and Done return Home.
- **Game files** owns the four paths the build reads: packed image, extracted disc, native
  library and game console. Each row has a folder button; image, disc and console have a
  picker that writes the new path to `launcher.ini`. The build reads the disc shown here
  through `-Disc`.
- **Build** runs `tools\wii-recomp-build.ps1` as a hidden child process with its output
  redirected to `logs\launcher-build.log`, tails the file every 300 ms, and maps the
  script's `== section` markers to a progress bar and stage name. Keep building leaves it
  running while you go back (the header pill says BUILDING); Open log shows the full file;
  Play is available again when the build stops and a runnable title exists. Closing the
  window while a build runs asks first, and a Job object with
  `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` stops the child if the launcher exits.

A `.wrd` image, an extracted disc folder (with `sys\main.dol` or `main.dol`) or
`wii-recomp.exe` can also be dropped on the window; it lands on Game files with the path
set.

## Play

- When the packed image is current, the console is started with the image alone:
  `wii-recomp.exe games\SO3EE9.wrd`, with `WII_RECOMP_NATIVE` removed. The console
  extracts the library from the image on first boot.
- Otherwise the extracted tree is used with `WII_RECOMP_NATIVE=<title.dll>`.
- `PATH` is prefixed by the MSYS2 CLANG64 bin directory (GLFW/SDL3/shaderc/Vulkan DLLs).
  Owned `WII_RECOMP_*` variables are stripped first, then the launcher's values are added.

The image is current when it is at least as new as `title.dll` and
`title-data\00010000-534f3345\shaders.bin`. Quit closes the launcher only; a running game
keeps running.

## Graphics settings

| Control | Values | Environment |
|---|---|---|
| Render scale | 1×, 1.5×, 2×, 3× | `WII_RECOMP_SCALE` (`3/2` for 1.5×) |
| Window size | 720p, 1080p, 1440p, 2160p | `WII_RECOMP_WINDOW=WxH` |
| FSR 1.0 | on, off | `WII_RECOMP_FSR=1/0` |
| Sharpness | 0.00–2.00, 0.05 steps | `WII_RECOMP_FSR_SHARPNESS` |
| Present mode | VSync, Mailbox, Immediate | `WII_RECOMP_PRESENT=fifo/mailbox/immediate` |
| Fullscreen | on, off | `WII_RECOMP_FULLSCREEN=1` |
| Frame rate overlay | on, off | `WII_RECOMP_FPS_OVERLAY=1` |

The window list is sized to the console's screen setting (IPL.AR in SYSCONF): 4:3 for a
4:3 console, 16:9 for widescreen, and the launcher shows the shape it will use. A window
that does not fit the desktop is reduced by the game, and the launcher reports the
effective size.

An FSR-enabled window that is at least as large as the internal picture is drawn through
AMD's EASU (edge-directed upscale) and RCAS (adaptive sharpening). At 1:1 RCAS still
sharpens; below 1:1 the runtime keeps the linear blit.

DLSS is not included. It requires NVIDIA's NGX SDK at build and run time, which this
project cannot redistribute; there is no honest way to expose it here. The FSR path is
the vendor-neutral equivalent.

## Controls

- **Test input** opens a live window with every button, both triggers, raw and shaped
  stick dots, and the deadzone circles.
- **Controls** opens the mapping editor. The Keyboard tab covers every action; clicking a
  row captures one key (the runtime also accepts a comma-separated list of up to four keys
  per action in `[keys]`). The Gamepad tab binds Wii buttons to host pad buttons — the
  binding applies to every layout and player — and adjusts the stick deadzone (0–0.9) and
  speed (0.25–4). Click a row, press a key or pad button, and conflicts are reported before
  they are saved; Clear, Defaults and the right-click pad picker round it out.
- The editor writes `controllers.ini` with the same keys the runtime reads, so the file
  never needs manual editing. Changes apply on the next launch.

## Config

`config/launcher.ini` (gitignored):

```ini
[paths]
msys=C:\msys64\clang64\bin
console=work\wii-recomp\recomp\build\wii-recomp.exe
title=work\wii-recomp\recomp\build-SO3EE9\title.dll
disc=extracted\SO3EE9\sys
image=work\wii-recomp\games\SO3EE9.wrd
saves=work\wii-recomp\recomp\build\saves

[options]
fullscreen=0
fsr=1
sharpness=0.20
scale=2
window=1280x720
present=fifo
overlay=1
```

Relative paths resolve against the repository root; absolute paths are used as-is.
Picker results are stored absolute. Settings are written as they change, so the next
launch starts with the same look.

Developer flags open a page directly: `--settings`, `--gamefiles`, `--build-page`,
`--build`, `--controls`, `--input-test`.

## Distribution

The launcher ships no game data. It expects the user to extract and build locally from
their own disc; the packed image contains disc data and generated code and stays local.
A per-user cache and an updater remain future work.

## Next steps

1. Move disc extraction into the launcher from the user's own image (ISO, RVZ, WBFS).
2. Read the disc header and check the six-character ID before building.
3. Keep a per-user cache keyed by disc hash plus recompiler version, rebuilding only what
   changed.
4. Update tools automatically; never update game-derived files.

```
%LOCALAPPDATA%\SeaOfStar-ToD\
  discs\<title-id>\
    extracted\
    generated\
    build\
  cache\
    shaders\
    symbols\
  saves\
  logs\
```
