<h1 align="center">SeaOfStar-ToD</h1>

<p align="center"><em>Rune Factory: Tides of Destiny (Wii), running natively on Windows x64 without an emulator.</em></p>

SeaOfStar-ToD is a static recompilation project. The game's PowerPC code is translated to C
ahead of time and compiled into a normal Windows program; the Wii's operating system, IOS,
disc access, input, audio and GX graphics are replaced by a native runtime. It is **not** an
emulator and not a decompilation: the game's own asset loaders still run on the original data.

> **No game files are included.** You supply your own legally dumped disc, and everything
> derived from it stays on your machine. See [Legal](#legal).

## Status

Playable baseline. Tested with the USA disc
`SO3EE9`; details and evidence in [docs/08-progress.md](docs/08-progress.md).

Working (measured):

- Boots through the safety screen, logos and Sofdec opening movie to the title screen, a
  New Game and 3D gameplay, with audio and input. A 34-minute run stayed alive.
- **Saves:** an imported save appears in the game's own Load menu; slots 11 and 12 were
  confirmed loading into gameplay.
- **Frame rate:** a 30 fps lock. In launcher play from the packed image the median frame is
  33.3 ms (p95 34.5 ms), with about 7 frames over 50 ms per minute.
- **Shader pre-bake:** the packed image carries 341 shader recipes collected by playing, so
  scenes draw without compiling shaders.
- **Upscaling:** real FSR 1.0 (EASU + RCAS) inside the runtime's Vulkan present path.
- **Launcher:** native Win32 window with Home, Settings, Game files and Build pages, a
  remappable controls editor, a crash report and an FPS overlay switch.

Known limits (not hidden):

- Recipe coverage is only as complete as the scenes played; it is not yet a full set.
- Scripted playtests at render scale 2 drop about one frame per second on the test GPU; the
  gap between scripted and free play at scale 1.5 is unexplained.
- **No 60 fps mode.** The game is frame-coupled at 30 fps; 60 fps would need engine-level
  interpolation or delta-time work (ADR-007).
- **No DLSS.** An experimental NGX probe runs, but the runtime has no motion vectors or
  jittered projection yet (ADR-012). The launcher has no DLSS switch.
- Only the USA build (`SO3EE9`) has been verified. PAL and Japanese discs are untested.
- The PlayStation 3 version is out of scope.

## Requirements

- Windows 10 or 11, x64.
- A Vulkan 1.3-capable GPU.
- Your own legally dumped Wii disc of Rune Factory: Tides of Destiny (verified: USA, `SO3EE9`).
- 16 GB RAM recommended; tens of GB free disk for the extracted disc, generated code and the
  packed image (about 2.4 GB).
- For now the runtime is built locally, so you also need the toolchain: MSYS2 CLANG64 with
  Ninja, Visual Studio 2022 Build Tools ("Desktop development with C++"), CMake 3.20+,
  Git and Python 3. `tools\check-env.ps1` reports what is present and what is missing.
  A prebuilt download is not available yet.

## Quick start

1. Build the launcher (see [Building](#building-from-source)) and run
   `build\launcher\SeaOfStar-ToD.exe`.
2. **Game files:** point it at your disc dump (extracted tree or image).
3. **Build:** run the in-window build. It translates the disc, compiles the native library and
   packs `SO3EE9.wrd`; Home shows **PRE-BAKED** once the image is current.
4. **Play.** Load saves from the game's own Load menu.

The launcher writes its settings to `config\launcher.ini` and the controls to
`controllers.ini`; neither needs hand editing.

## Building from source

```powershell
# Environment check, then local paths (config\game.json is gitignored)
Copy-Item config\game.example.json config\game.json
powershell -NoProfile -ExecutionPolicy Bypass -File tools\check-env.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tools\fetch-tools.ps1

# Disc extraction and the working path (the runtime in runtime\)
powershell -NoProfile -ExecutionPolicy Bypass -File tools\extract-disc.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tools\wii-recomp-build.ps1

# The launcher
powershell -NoProfile -ExecutionPolicy Bypass -File tools\build-launcher.ps1
```

The launcher's built-in default paths point at the maintainer's development clone (`work\wii-recomp\...`); on a plain clone copy `config\launcher.example.ini` to `config\launcher.ini`, whose paths use `runtime\...`.

`tools\wii-recomp-build.ps1` is what the launcher's Build page runs; it ends by packing
`runtime\games\SO3EE9.wrd` (disc at its original offsets, native library, shader
recipes). Commands and options: [docs/04-tooling.md](docs/04-tooling.md).

## Controls

Keyboard, mouse and gamepad work at once, with defaults that need no setup (WASD, Enter,
Backspace, mouse pointer). Everything is remappable in the launcher. See
[docs/controls.md](docs/controls.md).

## Project layout

| Path | Purpose |
|---|---|
| `launcher/` | The native Win32 launcher (`SeaOfStar-ToD.exe`) |
| `tools/` | PowerShell and Python entry points: environment check, extraction, build, packing, playtests, save import |
| `config/` | Local settings; `*.example.*` files are tracked, the rest is local |
| `docs/` | Documentation index in [docs/README.md](docs/README.md) |
| `runtime/` | The native runtime: a fork of [Zontex/wii-recomp](https://github.com/Zontex/wii-recomp) (GPL-3.0-or-later) copied in as plain files (no git history). Day-to-day development happens in a separate clone (`work\wii-recomp`, branch `seaofstar-tod`) and is synced in by `tools\sync-runtime.ps1` |
| `database/`, `game/` | A title-ID seed for the recompiler; where you may place your own dump (its contents are never committed) |
| `work/` | External tool clones and the maintainer's development clone of the runtime; gitignored |
| `extracted/`, `generated/`, `build/`, `logs/`, `analysis/` | Local, game-derived or build output; gitignored |

The on-disk folder is still called `rftod`; only the branding changed.

## Contributing

Bug reports with a launcher crash report or play log (remove personal paths first) are the
most useful thing. See [CONTRIBUTING.md](CONTRIBUTING.md).

## Legal

SeaOfStar-ToD is an unofficial fan project, not affiliated with or endorsed by Nintendo,
Marvelous, Natsume or Neverland. Rune Factory and related names belong to their owners.

This repository contains no game code, assets, keys, saves, disc images, generated code or
packed images, and none must be committed or shared. You must own the game and dump it
yourself. NVIDIA NGX/DLSS libraries are proprietary and are never redistributed here. See
[docs/06-legal.md](docs/06-legal.md).

## Credits

Built on DolRecomp, wii-recomp, nod and Dolphin, and inspired by the Longshot project. Full list with licences: [CREDITS.md](CREDITS.md).

## License

GPL-3.0-or-later, because the runtime builds on GPL-licensed components. See [LICENSE](LICENSE).
