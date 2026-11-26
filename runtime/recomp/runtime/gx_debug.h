// Switches for finding out why a picture is wrong on a device, all off unless asked for.
// WII_RECOMP_GX_DEBUG=name,name,... turns them on; each one named logs when it is first
// looked at ("gx debug: <name> on"), and a name nothing knows logs a warning once.
//
//   nodual        use the path for devices without dual-source blending, even where it exists
//   oldnodual     in that path, blend by the alpha to store (a constant alpha) and not the stages' alpha, as
//                 before. Without it the output's alpha is the stages' alpha where a draw blends with it,
//                 and the alpha to store is drawn by a second pass with only the alpha channel written
//                 (gx_vulkan_pipeline.c, vk_alpha_pass_state); a draw that doesn't blend with the stages'
//                 alpha stores its own in one pass
//   fullalphapass in that path, a second pass wherever a draw blends with the stages' alpha and stores an alpha, as before
//                 (gx_alpha_split.h has the cases that now need none: colour not written, a ZERO source alpha factor,
//                 a constant alpha of 0)
//   nobbox        shaders don't write the bounding box (as on a device that can't)
//   nopush        no push descriptors: textures take sets from pools
//   noaniso       no anisotropic filtering
//   nodepthlinear depth images are sampled without linear filtering
//   depthalways   every depth test passes (a draw still writes its depth)
//   nocull        no face is culled
//   noblend       nothing blends: a draw replaces what is there
//   depth=x8d24   the picture's depth is 24 bits (X8_D24) instead of 32-bit float
//   nostrip keepdepth depthevery noflat noflatdepth noearlydepth noside keepempty
//   noearlyread drawclears keepcopyback copiesoncard
//                 the same as the WII_RECOMP_NO_STRIP, _KEEP_DEPTH, _DEPTH_EVERY_PASS,
//                 _NO_FLAT_COPIES, _NO_FLAT_DEPTH, _NO_EARLY_DEPTH, _NO_SIDE, _KEEP_EMPTY_LAYERS,
//                 _NO_EARLY_READ, _DRAW_CLEARS, _KEEP_COPY_BACK and _COPIES_ON_CARD variables
//   report        print what the device offers again (it is printed once at start-up anyway)
#pragma once

int gx_debug(const char *name);          // Whether the switch is on.
const char *gx_debug_value(const char *name);  // For name=value switches: the value, or 0.
