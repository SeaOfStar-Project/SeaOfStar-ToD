// Without dual-source blending: how a draw's two alphas (the stages' alpha that the blend factors read, and the alpha
// to store, a constant alpha) get through a single colour output. Pure functions of the render state, so a test can
// compile this file alone (test/c/test_alpha_split.c).
//
// The reference is the dual-source formulation: the blend reads the stages' alpha S1 through the SOURCE1 factors, and
// the colour attachment's alpha channel becomes op(S0 * srcAlphaFactor, D * dstAlphaFactor), with S0 the alpha to store
// and D what is there. Without a second output the shader's output alpha is either S0 or S1:
//   - S1 ("stage" in the plan): the blend's SOURCE1 factors read it as SRC_ALPHA.
//   - S0: the alpha channel is made right by the blend's alpha factors.
// When the alpha channel needs both, the draw is drawn twice ("pass": an alpha-only draw with S0 first, nothing blended).
// The cases below need no second draw because the result is exactly the same number:
//   1. Colour is not written and no alpha factor reads S1: nothing reads S1 at all, so the output is S0 (no stage).
//   2. The source alpha factor is ZERO and the destination's doesn't read S1: op(S0 * 0, D * df) = op(0, D * df) whatever
//      S0 is, so the output can be S1 and the alpha channel still ends up as the dual-source one. In UNORM8 the product
//      S0 * 0 is exactly 0 for every S0 (no NaNs or infinities exist in the inputs), so the equality is bit exact.
//   3. A constant alpha of 0 (gx_alpha_zero_variant) is case 2: ONE/ZERO with S0 = 0 gives 0 * 1 + D * 0 = 0, and
//      ZERO/ZERO gives 0 as well.
// There is no exact single-pass form for a constant other than 0: the alpha channel is S1 * sf + D * df with sf and df
// from a fixed list (ZERO, ONE, S1, 1-S1, D, 1-D, constant, 1-constant). A result that is the same K for every S1 and D
// needs S1 * sf to be a constant for all S1, which only ZERO is; D * df likewise. So only 0 is reachable.
// WII_RECOMP_GX_DEBUG=fullalphapass brings the earlier rule back (a second pass wherever a SOURCE1 factor is read).
#pragma once

#include "gx_backend.h"

enum { GX_ALPHA_STAGE = 1, GX_ALPHA_PASS = 2 };

static inline int gx_is_stage_alpha_factor(int factor) { return factor == GX_FACTOR_SOURCE1_ALPHA || factor == GX_FACTOR_INVERSE_SOURCE1_ALPHA; }

// Bits of GX_ALPHA_*. blending: the blend is on (and not switched off for debugging). simple: the cases above are in use.
static inline unsigned gx_alpha_plan(const GxRenderState *state, int blending, int simple) {
  if (!blending || !state->blend_on) return 0;
  int colour_reads = gx_is_stage_alpha_factor(state->blend_source) || gx_is_stage_alpha_factor(state->blend_target);
  int alpha_reads = gx_is_stage_alpha_factor(state->blend_source_alpha) || gx_is_stage_alpha_factor(state->blend_target_alpha);
  if (!colour_reads && !alpha_reads) return 0;
  if (simple && !alpha_reads && !state->color_write) return 0;  // Case 1.
  unsigned plan = GX_ALPHA_STAGE;
  if (!state->alpha_write) return plan;
  // Case 2 (alpha_reads is 0 there, or the source factor would not be ZERO).
  if (simple && state->blend_source_alpha == GX_FACTOR_ZERO && !gx_is_stage_alpha_factor(state->blend_target_alpha)) return plan;
  return plan | GX_ALPHA_PASS;
}

// The state of a draw with a constant alpha of 0 that the blend would otherwise need a second pass for. 0 if it doesn't apply.
static inline int gx_alpha_zero_variant(const GxRenderState *state, GxRenderState *variant) {
  if (!state->blend_on || !state->alpha_write || state->blend_subtract_alpha) return 0;
  if (state->blend_source_alpha != GX_FACTOR_ONE || state->blend_target_alpha != GX_FACTOR_ZERO) return 0;
  if (!gx_is_stage_alpha_factor(state->blend_source) && !gx_is_stage_alpha_factor(state->blend_target)) return 0;
  *variant = *state;
  variant->blend_source_alpha = GX_FACTOR_ZERO;
  return 1;
}
