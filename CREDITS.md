# Credits

This project builds on work by other people. Keep these notices when redistributing.

## Direct dependencies

| Project | Author | License | Use |
|---|---|---|---|
| [DolRecomp](https://github.com/ExpansionPak/DolRecomp) | ExpansionPak | GPL-3.0-or-later | PowerPC → C recompiler |
| [nod](https://github.com/encounter/nod) | encounter | MIT | Disc extraction (`nodtool`) |
| [wii-recomp](https://github.com/Zontex/wii-recomp) | Zontex and contributors | GPL-3.0-or-later | The runtime in `runtime/` is derived from this project (copied in as plain files, derived from upstream commit `55fb408` of <https://github.com/Zontex/wii-recomp>; upstream and fork history are not kept in this repository). Its `LICENSE` and notices are kept unchanged in `runtime/`; the upstream authors and contributors remain the copyright holders of their work, and our changes are GPL-3.0-or-later too |
| [Dolphin](https://github.com/dolphin-emu/dolphin) | Dolphin Emulator Project | GPL-2.0-or-later | VideoCommon reference, oracle |
| [SDL](https://www.libsdl.org/) 3.4.20 | Sam Lantinga and SDL contributors | zlib | Window, input and audio on Android. The Java glue in `android/app/src/main/java/org/libsdl/app/` is copied unmodified from SDL's `android-project` (each file carries SDL's licence text; `LICENSE-SDL.txt` beside them). The Android build also links SDL's `libSDL3.so`, built on the user's PC, never committed |
| [shaderc](https://github.com/google/shaderc) | Google | Apache-2.0 | Built on the user's PC from the NDK's sources for the Android build (`tools\android-build.ps1`); not tracked or distributed here |
| [Gradle wrapper](https://gradle.org/) | Gradle Inc. | Apache-2.0 | `android/gradlew`, `gradlew.bat` and `gradle/wrapper/gradle-wrapper.jar` |
| [Vulkan-ValidationLayers](https://github.com/KhronosGroup/Vulkan-ValidationLayers) | Khronos Group and LunarG | Apache-2.0 | Optional, debug builds only: a local debug APK can bundle `libVkLayer_khronos_validation.so` (`-PvalidationLayer`, `VVL_LAYER` in `tools\android-push.ps1`). It is not tracked here and not distributed |

## Inspiration

| Project | Author | Use |
|---|---|---|
| [Longshot](https://git.magicbean.dev/MagicBeanDev/Longshot) | MagicBeanDev | Static-recompilation pipeline and launcher model |
| [N64Recomp](https://github.com/N64Recomp/N64Recomp) | N64Recomp contributors | Static recompilation tooling |
| [Wii Party Recomp](https://github.com/ExpansionPak/Wii-Party-Recomp) | ExpansionPak | Playable Wii recompilation reference |
| Mario Kart Wiicompiled | — | First Wii static recompilation |

## Game

Rune Factory: Tides of Destiny / Rune Factory: Oceans — developed by Neverland, published by
Marvelous Entertainment (Japan) and Natsume (North America). Wii version, 2011.

This project is not affiliated with or endorsed by any of the rights holders.
