# 03 — Runtime Checklist

Runtime subsystem checklist. Mark `[x]` only after it has been tested in the game, not just
"written".

## 1. Memory and boot

- [ ] MEM1: `0x80000000`, 24 MB.
- [ ] MEM2: `0x90000000`, 64 MB.
- [ ] MMIO: `0xCC000000` (VI, PI, MI, DSP, AI, EXI, SI, IPC).
- [ ] Arena setup and `__OSArenaHi`/`Lo`.
- [ ] FST (file string table) and BI2 are read.
- [ ] Boot info: disc ID, region, arena.
- [ ] Stack and exception vectors.
- [ ] Guest → host address translation is consistent.
- [ ] Big-endian 8/16/32/64-bit loads and stores are correct.
- [ ] Unaligned access is handled (or logged).
- [ ] Invalid memory access produces a useful log.

## 2. OS and threads

- [ ] `OSInit`.
- [ ] `OSCreateThread`, `OSResumeThread`, `OSExitThread`.
- [ ] `OSYieldThread`, `OSSleepThread`, `OSWakeupThread`.
- [ ] Priority scheduling (cooperative).
- [ ] Mutex: `OSInitMutex`, `OSLockMutex`, `OSUnlockMutex`.
- [ ] Condition: `OSInitCond`, `OSWaitCond`, `OSSignalCond`.
- [ ] Message queue: `OSInitMessageQueue`, `OSSendMessage`, `OSReceiveMessage`.
- [ ] `OSGetTime`, `OSGetTick`, time base.
- [ ] `OSReport` / `OSPanic` → host log.
- [ ] Thread cleanup and join.

## 3. Interrupts and timers

- [ ] Exception vector table.
- [ ] Decrementer exception.
- [ ] `OSSetAlarm`, `OSCancelAlarm`.
- [ ] PI interrupt (disc, EXI, AI, DSP, VI, SI).
- [ ] `OSEnableInterrupts` / `OSDisableInterrupts`.
- [ ] Per-source interrupt masking.
- [ ] No interrupt storms.
- [ ] Timing accurate enough for DVD reads and audio.

## 4. VI (video interface)

- [ ] VI registers: `VI_DISPCTRL`, `VI_HCTRL`, `VI_VCTRL`, timing.
- [ ] Retrace interrupt.
- [ ] XFB address and stride.
- [ ] NTSC 60 Hz / PAL 50 Hz.
- [ ] 4:3 / 16:9 aspect.
- [ ] Frame present to the window.
- [ ] Frame pacing (no 100% CPU spin).

## 5. DI / DVD

- [ ] Read files from `files/` (not the raw ISO).
- [ ] Async read + interrupt.
- [ ] `DVDReadAbsAsyncPrio`, `DVDSeekAbsAsyncPrio`.
- [ ] Error handling (missing file, read failure).
- [ ] Cover channel / disc ID.
- [ ] Read speed does not make the game time out.

## 6. IOS / IPC

- [ ] IPC ring buffer (`HW_IPC_*`).
- [ ] `/dev/di` — disc.
- [ ] `/dev/fs` — filesystem/NAND.
- [ ] `/dev/es` — title/content.
- [ ] `/dev/stm` — system timing.
- [ ] `/dev/usb` — if the game uses it.
- [ ] `/dev/net` — if the game uses it.
- [ ] IOS reset/open/close/read/write/ioctl.
- [ ] IOS error codes are returned correctly.

## 7. NAND / saves

- [ ] Virtual NAND layout.
- [ ] `/title/00010000/<id>/data/`.
- [ ] SYSCONF (settings, region, video).
- [ ] Save file create/read/write/delete.
- [ ] Saves from Dolphin can be imported.
- [ ] New saves can be read by Dolphin (for verification).

## 8. EXI / SI

- [ ] EXI RTC (SRAM) + checksum.
- [ ] Memory card (if the game uses it).
- [ ] SI joybus: controller ID, origin, poll.
- [ ] GC pad (if used).

## 9. WPAD / input

- [ ] Bluetooth HID / virtual remote.
- [ ] Report 0x30 (Remote buttons).
- [ ] Report 0x31/0x35/0x36 (Remote + Nunchuk / MotionPlus).
- [ ] IR pointer (4 points or interpolation).
- [ ] Nunchuk stick, buttons, accelerometer.
- [ ] Rumble.
- [ ] Classic Controller (if the game supports it).
- [ ] Keyboard/mouse/gamepad mapping.
- [ ] Deadzone and sensitivity.

## 10. Audio

- [ ] AI registers and DMA.
- [ ] AX voices and mixer.
- [ ] DSP ADPCM decode.
- [ ] Sample rate (32 kHz / 48 kHz).
- [ ] Microcode: standard AX.
- [ ] Custom microcode: interpret/recompile/HLE.
- [ ] Audio timing does not drift.
- [ ] No clicks/pops on start/stop.

## 11. GX

### Command stream

- [ ] CP FIFO parse.
- [ ] Opcode call/return.
- [ ] XF/BP command addressing.
- [ ] FIFO wrap and alignment.

### Transform

- [ ] XF matrices (position, normal, texture).
- [ ] Projection and viewport.
- [ ] Lighting (if used).
- [ ] Texture coordinate generation.
- [ ] Culling and clipping.

### TEV

- [ ] Color/alpha combiner.
- [ ] Constants and registers.
- [ ] Texture stages.
- [ ] Indirect stages (if used).
- [ ] Fog.
- [ ] Alpha test.
- [ ] Blending.

### Textures

- [ ] CMPR.
- [ ] RGB565, RGB5A3.
- [ ] RGBA8.
- [ ] I4, I8, IA4, IA8.
- [ ] CI4, CI8, CI14X2 + TLUT.
- [ ] Mipmaps.
- [ ] Wrap modes and filtering.

### Framebuffer

- [ ] EFB clear.
- [ ] EFB copy to texture.
- [ ] EFB copy to XFB.
- [ ] Z-buffer.
- [ ] Scissor and viewport.
- [ ] XFB present.
- [ ] Internal resolution scale.

## 12. Debug and observability

- [ ] Log levels (error/warn/info/trace).
- [ ] Guest PC on every crash.
- [ ] Symbol map lookup (address → function).
- [ ] Frame capture (PNG).
- [ ] Audio capture (WAV).
- [ ] GX FIFO replay.
- [ ] Memory dump on demand.
- [ ] Thread/CPU usage stats.
- [ ] `--headless` mode for automated boot tests.

## 13. Build and distribution

- [ ] Reproducible build.
- [ ] No game files in the repo.
- [ ] Launcher builds from the user's ISO.
- [ ] Crash reports contain no sensitive data.
- [ ] License and third-party notices.

## 14. Game-specific: CRI middleware and RSO (verified from this disc)

The USA disc (`SO3EE9`) contains CRI middleware and one runtime-loaded module. These are
not generic Wii subsystems, so they get their own checklist.

- [ ] RSO loader: `files/Sofdec.rso` (552,192 bytes) is loaded from the disc at runtime.
      The runtime must relocate and call it like a REL, even though the game has no `.rel`
      files.
- [ ] `.sel` files (3 small ones) — identify what loads them before assuming they are data.
- [ ] CPK container: `files/Sound/stream/*.cpk` (~989 MB). The game's own CRI code parses
      it; the runtime only needs correct file reads.
- [ ] ADX/HCA decode: confirm it runs on the CPU (no custom DSP microcode). If
      `AXRegister*` receives a non-standard ucode, add an interpreter.
- [ ] Sofdec video: `files/Movie/*.sfd` (~508 MB). Stub playback first so boot does not
      depend on it; implement or skip later.
- [ ] `files/RFF2.dat` (719,998,976 bytes) and `RFF2.bin` (28,552 bytes) — the main asset
      archive pair; confirm the access pattern (sequential vs random).
- [ ] Self-modifying code: 17 candidate ranges in `SO3EE9_smc.txt`. For each, either
      translate the generated range ahead of time or keep the interpreter fallback.
- [ ] Confirm the AX microcode is the standard Nintendo one. CRI decodes audio on the CPU,
      so a custom DSP ucode should not be required; verify before relying on that.

