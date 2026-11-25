# 05 — Debugging

The rule for this project: **never debug the recompiled build without a Dolphin reference
for the same frame.**

## Dolphin as the oracle

Install Dolphin separately. It is not part of the build and is not shipped.

Settings to enable:

- **Dump Frames** — PNG per frame, useful for visual comparison.
- **Dump Audio** — WAV, with DSP LLE selected so the reference is the real DSP output.
- **Dump GX FIFO** — `.dff` files, one per frame or on demand.
- **Save states** — at logo, menu, intro, first gameplay, first dungeon.
- **Input movie** — record a deterministic replay for the same button sequence on both
  builds.

Keep the Dolphin game ID and revision identical to the dump used for recompilation. A
different revision means different code addresses and different behavior.

## Frame comparison

1. Run the same input movie in Dolphin and in the recompiled build.
2. Dump frame N from both.
3. Compare:
   - exact hash (only for deterministic, identical rendering);
   - perceptual hash or SSIM (tolerates driver differences);
   - region crops for HUD, 3D scene, and effects.

A black frame is still useful: it tells you whether VI/XFB works before GX does.

## GX FIFO replay

Dolphin's `.dff` contains the exact command stream for a frame. Feed it to the GX backend
without running the game:

```
runtime --replay-fifo frame_01234.dff
```

This isolates renderer bugs from game-logic bugs. If the replay matches Dolphin but the game
does not, the problem is earlier: CP FIFO generation, memory, or timing.

## Audio comparison

- Dump WAV from Dolphin with DSP LLE.
- Dump WAV from the runtime.
- Compare RMS, spectrum, and sample count per second.
- A pitch shift means the sample rate is wrong. Crackle means buffer underrun or a bad
  resampler. Silence means AX voices are not started or the AI DMA never runs.

RF:ToD uses CRI audio (CPK archives, ADX/HCA). CRI decodes on the CPU, so the runtime needs a
correct AX mixer and AI DMA, not a custom DSP microcode interpreter. That is good news, but
verify it: if the game loads a custom ucode through `AXRegister*`, the plan changes.

## Memory checkpoints

At a known frame, dump MEM1 and MEM2 from Dolphin's debugger and from the runtime:

```
0x80000000 .. 0x81800000   MEM1 (24 MB)
0x90000000 .. 0x94000000   MEM2 (64 MB)
```

Compare in 4 KB blocks. The first divergent block usually points at the subsystem that wrote
it. This catches logic divergence before it becomes a crash.

## Crash triage

1. Log the guest PC and LR on every fault.
2. Map the address through the generated symbol table (`SO3EE9_symbols.h`, or the chunk
   function names).
3. If the address is not translated, the interpreter fallback should log it once.
4. Add an HLE stub with logging for the SDK function that was called.
5. Re-run with the same input movie and compare the log order to Dolphin's.

Useful runtime flags (planned):

```
--log-level debug
--trace-guest-pc
--trace-hle
--dump-memory <addr> <size> <frame>
--replay-fifo <file>
--headless
```

## wii-recomp diagnostics

If wii-recomp is used as a baseline, it has its own instrumentation:

| Variable | Effect |
|---|---|
| `WII_RECOMP_ACTIVITY=1` | Per-second activity counts and slow frames |
| `WII_RECOMP_DUMP_AFTER=<s>` | List what each thread waits for |
| `WII_RECOMP_FPS_LOG=1` | Frames per second |
| `WII_RECOMP_DISC_LOG=1` | Disc reads with offsets and sizes |
| `WII_RECOMP_FRAME_LOG=1..3` | Frame timing and current thread |
| `WII_RECOMP_NO_MODULES=all` | Leave RSO/REL code to the interpreter |
| `WII_RECOMP_CONTROL=<pipe>` | Script input: `press a`, `stick x y`, `shot <file>` |

## Self-modifying code

`generated/SO3EE9_generated/SO3EE9_smc.txt` lists 17 candidate ranges. For each:

- disassemble the range in Ghidra;
- find the writer;
- decide: translate the generated code ahead of time, patch the check, or accept the
  interpreter fallback.

Do not ignore the list. A game that patches code at runtime will silently execute stale host
code if the runtime does not invalidate the translated block.
