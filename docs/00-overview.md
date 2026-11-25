# 00 — Overview

## Goal

Run **Rune Factory: Tides of Destiny (Wii)** as a native Windows x64 program, without an
emulator, from the user's own disc dump.

Approach: *static recompilation*.

1. Translate `main.dol` (PowerPC) to C.
2. Compile the C with the host compiler.
3. Replace the Wii OS, IOS, input, audio, and GPU with a native runtime.
4. Let the game's own asset loaders read the original files.

## Target

| Item | Value |
|---|---|
| Game | Rune Factory: Tides of Destiny / Oceans |
| Source platform | Wii (Broadway, big-endian) |
| Title ID | `SO3EE9` (USA, verified from the disc header); check the disc header for PAL/JP |
| Host | Windows 10/11 x64 |
| Renderer | Vulkan (primary), D3D11/OpenGL (fallback) |
| Runtime language | C++20 |
| License | GPL-3.0-or-later |

## Scope

In scope:

- PowerPC CPU recompilation to C.
- Revolution OS runtime: threads, interrupts, alarms, memory, VI.
- IOS HLE: disc, filesystem, NAND, IPC.
- GX → Vulkan: CP/XF/BP, TEV, textures, EFB/XFB.
- Input: Wii Remote + Nunchuk (and Classic Controller if the game uses it).
- Audio: AX + DSP ADPCM; custom microcode if present.
- NAND saves.
- Enhancements: internal resolution, widescreen, controller remap, save import.

Out of scope (for now):

- The PS3 / Cell SPE version.
- A generic emulator.
- Match-based decompilation.
- Distribution of game files, assets, keys, or generated code.
- Multiplayer / online play.

## Success criteria

Minimum:

- [ ] Boot to the logo/menu.
- [ ] Enter gameplay and move the character.
- [ ] Audio output.
- [ ] Save/load works.
- [ ] No crash during 30 minutes of gameplay.

Stretch:

- [ ] Full playthrough without crashes.
- [ ] Stable 60 fps in heavy scenes.
- [ ] Internal resolution ≥ 2x.
- [ ] Widescreen without culling bugs.
- [ ] Import saves from Dolphin.

## Technical constraints to keep in mind

- The Wii is **big-endian**; every guest memory access needs a byte swap.
- Broadway has **paired singles** (PS0/PS1) and GQR quantization.
- The Wii SDK is **statically linked** into the binary; there is no import table like on
  Switch. SDK functions must be replaced by address, signature, or hash.
- GX is **fixed-function**; there is no shader bytecode like NVN. Host shaders are generated
  from TEV state.
- MEM1 is 24 MB and MEM2 is 64 MB; there is no virtual memory.
- IOS is a separate ARM coprocessor; HLE at the IPC/device level is the practical choice.
- Many Wii games ship **custom DSP microcode**; this is the biggest audio risk.

## Status

Scaffold, extraction, and CPU recompilation are complete. The wii-recomp baseline boots
the USA build all the way to 3D gameplay with audio and input, so the project now
productizes that runtime instead of writing one from scratch (ADR-010).

Verified baseline:

| Item | Value |
|---|---|
| Disc ID | `SO3EE9` |
| Entry point | `0x80004050` |
| Text | 5.29 MB |
| Instructions | 1,353,840 decoded, 0 unknown |
| Generated C | 337 MB, 331 chunks |
| REL | none |
| RSO | `Sofdec.rso` (CRI video), linked at runtime |
| Audio | CRI CPK, ADX/HCA |
| Video | CRI Sofdec `.sfd` |
| Boot result | safety screen → logos → opening movie → title → New Game → 3D gameplay |
| Frame rate | ~30 fps, worst gameplay frame ~50 ms |

See [02-milestones.md](02-milestones.md) and `analysis/wii-recomp-baseline.md`. A dated
snapshot of what is verified and what is still open is in [08-progress.md](08-progress.md).
