# GX to Vulkan notes

Design notes for the GX to Vulkan renderer. The implementation lives in `runtime/recomp/runtime/gx*.c`
(it began as an early plan for a from-scratch backend; ADR-010 adopted the wii-recomp runtime instead).

GX is fixed-function. There is no shader bytecode to recompile; the host shader is generated
from the current TEV state. That makes this closer to Dolphin's VideoCommon than to a modern
console renderer.

## Pipeline

```
CP FIFO  ──► command decode ──► XF (transform) ──► rasterizer state (BP)
                                                    │
                                                    ├─ TEV state ──► SPIR-V shader (cached)
                                                    ├─ textures ──► decoded images
                                                    └─ EFB ──► XFB ──► window
```

## Subsystems

### Command stream

- CP FIFO parse, call/return, wrap, alignment.
- XF and BP register writes.
- Display lists and repeated draws.

### Transform

- Position, normal, and texture matrices.
- Projection, viewport, scissor.
- Lighting and texture coordinate generation.
- Culling and clipping.

### TEV

- Color and alpha combiners, multiple stages.
- Constant registers and Konst colors.
- Texture and raster stages.
- Indirect stages (if the game uses them).
- Fog, alpha test, blending, z-texture.

Generate a SPIR-V shader keyed by a hash of the TEV state. Cache compiled pipelines.

### Textures

- CMPR, RGB565, RGB5A3, RGBA8.
- I4, I8, IA4, IA8.
- CI4, CI8, CI14X2 with TLUT.
- Mipmaps, wrap modes, filtering.

### Framebuffer

- EFB clear.
- EFB copy to texture (required for water, reflections, and post effects).
- EFB copy to XFB.
- XFB present with correct stride and aspect.
- Internal resolution scale (the main visual win over Dolphin).

## Bring-up order (historical plan)

1. EFB clear color reaches the window.
2. 2D logo/menu: CP, BP, one or two TEV stages, basic textures.
3. 3D scene: XF, full TEV, z-buffer, culling.
4. Effects: EFB copy, fog, alpha test, indirect stages.
5. Performance: pipeline cache, texture cache, frame pacing.

## Reference material

- Dolphin `VideoCommon` (GPL-2.0-or-later) — TEV shader generation and texture decoding.
- `runtime/recomp/runtime/gx*` (GPL-3.0-or-later) — the Vulkan GX implementation in use.
- Dolphin GX FIFO dumps (`.dff`) — replay a real frame through the backend without running
  the game.

Both references are GPL-compatible with this project. Keep attribution and license notices.
