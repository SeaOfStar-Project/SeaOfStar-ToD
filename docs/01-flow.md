# 01 — Flow

The full pipeline from disc to native executable.

```
[0] Your own disc
     │ CleanRip / existing dump
     ▼
    game/SO3EE9.rvz
     │ wit / nodtool / Dolphin
     ▼
    extracted/SO3EE9/
      ├─ sys/main.dol
      ├─ sys/bi2.bin, sys/boot.bin
      ├─ files/            (assets, possibly *.rel)
      └─ ...
     │
[1] Analysis
     │ DolRecomp analyzer / Ghidra / dtk / Dolphin debugger
     │ → entry point, SDK, functions, RELs, audio middleware
     ▼
    analysis/  (symbol maps, notes, dumps)
     │
[2] CPU recompilation
     │ DolRecomp / NWiiRecomp / gcrecomp / wii-recomp
     ▼
    generated/  (C/C++, one guest function per host function)
     │
[3] Runtime / HLE
     │ OS, threads, interrupts, MEM1/MEM2, VI, DI, IOS, NAND,
     │ WPAD, AX/DSP, GX
     ▼
    runtime/recomp/build/wii-recomp.exe + games/SO3EE9.wrd (packed image)
     │
[4] Bring-up + Dolphin oracle
     │ log → VI → GX clear → 2D → 3D → input → audio → save
     ▼
[5] Polish
         resolution, widescreen, 60 fps, mods, save import
```

## Phase 0 — Dump and baseline

**Goal:** a clean dump and a reference oracle.

Steps:

1. Dump your own disc (CleanRip on a Wii, or an existing personal dump).
2. Extract:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File tools\extract-disc.ps1
   ```

   The script uses `wit` or `nodtool`. If neither is installed, follow the Dolphin
   instructions (`right-click the game → Extract Files`).
3. Verify that `sys/main.dol` exists and record its SHA-256 (the script writes
   `analysis/disc-manifest.json`).
4. Set up Dolphin:
   - Enable **Dump Frames** and **Dump Audio** (DSP LLE).
   - Record an input movie for deterministic replay.
   - Create savestates at: logo, menu, intro, first gameplay, first dungeon.
   - Note the native frame rate and VI settings (NTSC/PAL, 4:3/16:9).

**Definition of done:** `extracted/<id>/sys/main.dol` exists, the manifest is written, and
Dolphin can run the same dump and produce reference dumps.

## Phase 1 — Binary analysis

**Goal:** a map of the binary before writing the runtime.

What to find:

- Entry point (usually `0x80003100`) and init order: `__start` → `OSInit` → `main`.
- SDK version: strings such as `Revolution`, `RVL`, `OS`, `AX`, `GX`, `WPAD`, `NAND`, `IPC`.
- SDK functions called by the game (later replaced by HLE).
- `*.rel` modules and runtime overlays; record their load addresses.
- Audio middleware: `CRI`, `ADX`, `HCA`, `Sofdec`, `BGM`, `SE`.
- Jump tables, vtables, and indirect calls that need special handling.

Tools:

```powershell
# DolRecomp (prints every option when run without arguments)
work\DolRecomp\build\Release\dolrecomp.exe

# Ghidra: import main.dol as raw PowerPC big-endian,
# base address 0x80000000, then analyze from the entry point.
```

Store outputs in `analysis/`:

- `disc-manifest.json`
- `functions.csv` (address, size, provisional name)
- `sdk-calls.md` (discovered SDK functions)
- `audio-notes.md` (middleware/microcode)
- `rel-layout.md` (if RELs exist)

**Definition of done:** main functions, SDK calls, and REL/audio status are documented.

## Phase 2 — CPU recompilation

**Goal:** C that compiles and can be called by the runtime.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\recompile.ps1
```

Output:

```
generated/
  SO3EE9_generated/
    SO3EE9.c
    SO3EE9.h
    SO3EE9_smc.txt
    chunks/            (331 files)
```

Measured result: 1,353,840 instructions decoded, 0 unknown, 337 MB of C, 17
self-modifying-code candidates.

Checklist:

- [ ] Big-endian loads/stores are correct.
- [ ] Paired singles + GQR are supported.
- [ ] `mftb`, decrementer, `sync`, `isync`, and cache instructions are handled correctly.
- [ ] Indirect branches go through the function table.
- [ ] Mid-function entries are handled (`switch(pc)`).
- [ ] Self-modifying code is reported, not silently ignored.
- [ ] An interpreter fallback exists for code loaded at runtime.

**Definition of done:** all main functions are recompiled and the generated C compiles as
objects (a full runtime link is not required yet).

## Phase 3 — Runtime / HLE

**Goal:** boot and run the main loop.

Implementation order (details in [03-runtime-checklist.md](03-runtime-checklist.md)):

1. Memory map + boot + `OSInit`.
2. Threads, mutexes, message queues, alarms, interrupts.
3. VI + XFB (black screen).
4. DI/DVD + IOS (file reads).
5. GX: clear → 2D → 3D.
6. WPAD/SI input.
7. AX/DSP audio.
8. NAND saves.
9. EXI/RTC.

**Definition of done:** the main loop runs, frames render, and input is read.

## Phase 4 — Bring-up and oracle

**Goal:** match Dolphin's behavior.

Methods:

- **FIFO replay:** dump the GX FIFO (`.dff`) from Dolphin, replay it through the GX
  implementation, and compare frames.
- **Frame hash / SSIM:** run the same input movie, dump frame N, and compare.
- **Audio diff:** WAV from Dolphin vs. the runtime; compare RMS/spectrogram.
- **Memory checkpoint:** dump MEM1/MEM2 at a specific frame and compare.
- **Crash triage:** guest address → function, then add logging HLE stubs.

**Definition of done:** milestones M4–M10 in [02-milestones.md](02-milestones.md) pass.

## Phase 5 — Polish

- Internal resolution scaling (EFB scale).
- Widescreen + culling fixes.
- 60 fps only after auditing the timestep.
- HD texture packs, mod loader.
- Save import/export with Dolphin.
- Controller remap, keyboard/mouse.
- Fast boot, shader cache, frame pacing.
- Launcher: the user supplies the disc and builds locally.

**Definition of done:** a stable build that can be played from start to finish without crashes.
