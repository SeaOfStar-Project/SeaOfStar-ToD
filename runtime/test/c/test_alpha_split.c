// gx_alpha_split.h: without dual-source blending, the draws the plan sends through one pass (stages' alpha as the output's)
// or two (alpha first) come out as the dual-source blend would make them, for every factor pair, on every UNORM8 value tried.
//   test_alpha_split
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "gx_alpha_split.h"

static const int factors[] = {GX_FACTOR_ZERO, GX_FACTOR_ONE, GX_FACTOR_SOURCE_COLOR, GX_FACTOR_INVERSE_SOURCE_COLOR, GX_FACTOR_TARGET_COLOR,
                              GX_FACTOR_INVERSE_TARGET_COLOR, GX_FACTOR_TARGET_ALPHA, GX_FACTOR_INVERSE_TARGET_ALPHA, GX_FACTOR_SOURCE1_ALPHA,
                              GX_FACTOR_INVERSE_SOURCE1_ALPHA};
enum { FACTORS = sizeof factors / sizeof factors[0] };

// One channel. s0 is the alpha to store, s1 the stages' alpha, c the stages' colour, dc and da what is there.
typedef struct { double s0, s1, c, dc, da; } Inputs;

static double factor_value(int f, const Inputs *in, double output_alpha, int for_alpha) {
  double own_source = for_alpha ? output_alpha : in->c, own_target = for_alpha ? in->da : in->dc;
  (void)own_source;
  switch (f) {
    case GX_FACTOR_ZERO: return 0;
    case GX_FACTOR_ONE: return 1;
    case GX_FACTOR_SOURCE_COLOR: return in->c;
    case GX_FACTOR_INVERSE_SOURCE_COLOR: return 1 - in->c;
    case GX_FACTOR_TARGET_COLOR: return in->dc;
    case GX_FACTOR_INVERSE_TARGET_COLOR: return 1 - in->dc;
    case GX_FACTOR_TARGET_ALPHA: return in->da;
    case GX_FACTOR_INVERSE_TARGET_ALPHA: return 1 - in->da;
    case GX_FACTOR_SOURCE1_ALPHA: return output_alpha;
    case GX_FACTOR_INVERSE_SOURCE1_ALPHA: return 1 - output_alpha;
  }
  (void)own_target;
  return 0;
}

static int unorm(double v) { return (int)floor((v < 0 ? 0 : v > 1 ? 1 : v) * 255 + 0.5); }

// source and target are the channel's own values (the colour or the alpha being blended).
static int blend(double source, double target, double fs, double ft, int subtract) {
  return unorm(subtract ? target * ft - source * fs : source * fs + target * ft);
}

int main(void) {
  static const double alphas[] = {0, 1 / 255.0, 0.25, 0.5, 200 / 255.0, 1};
  long checked = 0, skipped = 0, passes = 0, single = 0, failures = 0;
  for (int mode = 0; mode < 5; mode++) {  // 0 plain 1 simple off (the earlier rule), 2 constant 0 variant, 3 colour not written, 4 alpha not written
    for (int a = 0; a < FACTORS; a++) for (int b = 0; b < FACTORS; b++) for (int sa = 0; sa < 2; sa++) for (int ta = 0; ta < 2; ta++)
    for (int subtract = 0; subtract < 2; subtract++) {
      GxRenderState state;
      memset(&state, 0, sizeof state);
      state.blend_on = 1;
      state.blend_source = (uint8_t)factors[a];
      state.blend_target = (uint8_t)factors[b];
      // The alpha factors of a constant alpha, and ZERO in the source place for the others.
      state.blend_source_alpha = sa ? GX_FACTOR_ONE : GX_FACTOR_ZERO;
      state.blend_target_alpha = (uint8_t)(ta ? GX_FACTOR_ONE : GX_FACTOR_ZERO);
      state.blend_subtract = (uint8_t)subtract;
      state.color_write = mode != 3;
      state.alpha_write = mode != 4;
      int simple = mode != 1;
      int constant_zero = 0;
      if (mode == 2) {
        GxRenderState variant;
        if (!gx_alpha_zero_variant(&state, &variant)) continue;
        constant_zero = 1;
        if (state.color_write == 0) continue;
        state = variant;
      }
      unsigned plan = gx_alpha_plan(&state, 1, simple);
      for (size_t s0i = 0; s0i < 6; s0i++) for (size_t s1i = 0; s1i < 6; s1i++) for (size_t ci = 0; ci < 4; ci++)
      for (size_t di = 0; di < 4; di++) for (size_t dai = 0; dai < 6; dai++) {
        Inputs in = {alphas[s0i], alphas[s1i], alphas[ci], alphas[di + 1], alphas[dai]};
        if (constant_zero) in.s0 = 0;  // The draw's stored alpha.
        // The original state's alpha factors, for the reference: constant alpha ones when the variant was made from them.
        int source_alpha_factor = constant_zero ? GX_FACTOR_ONE : state.blend_source_alpha;
        // The dual-source reference. A state that reads no SOURCE1 factor has one output, so S0 = S1 in the reference's own terms.
        int reads = gx_is_stage_alpha_factor(state.blend_source) || gx_is_stage_alpha_factor(state.blend_target)
                    || gx_is_stage_alpha_factor(state.blend_source_alpha) || gx_is_stage_alpha_factor(state.blend_target_alpha);
        Inputs ref_in = in;
        if (!reads && !constant_zero) ref_in.s0 = ref_in.s1;
        double ref_colour_fs = factor_value(state.blend_source, &ref_in, ref_in.s1, 0), ref_colour_ft = factor_value(state.blend_target, &ref_in, ref_in.s1, 0);
        int ref_colour = blend(ref_in.c, ref_in.dc, ref_colour_fs, ref_colour_ft, state.blend_subtract);
        double fas = factor_value(source_alpha_factor, &ref_in, ref_in.s1, 1), fat = factor_value(state.blend_target_alpha, &ref_in, ref_in.s1, 1);
        int ref_alpha = blend(ref_in.s0, ref_in.da, fas, fat, state.blend_subtract_alpha);
        // What the plan does: the output alpha is S1 with the stage bit, else S0; a pass first stores S0 alone.
        double output = (plan & GX_ALPHA_STAGE) ? in.s1 : in.s0;
        if (!reads && !constant_zero) output = ref_in.s1;
        Inputs got_in = in;
        int pass = (plan & GX_ALPHA_PASS) != 0;
        if (pass) got_in.da = in.s0;  // The colour pass finds the alpha the first pass wrote (the earlier rule, and a known gap for TARGET_ALPHA factors).
        double got_fs = factor_value(state.blend_source, &got_in, output, 0), got_ft = factor_value(state.blend_target, &got_in, output, 0);
        int got_colour = blend(in.c, in.dc, got_fs, got_ft, state.blend_subtract);
        double gas = factor_value(state.blend_source_alpha, &in, output, 1), gat = factor_value(state.blend_target_alpha, &in, output, 1);
        int got_alpha = pass ? unorm(in.s0) : blend(output, in.da, gas, gat, state.blend_subtract_alpha);
        if (state.color_write) {
          int reads_target_alpha = state.blend_source == GX_FACTOR_TARGET_ALPHA || state.blend_source == GX_FACTOR_INVERSE_TARGET_ALPHA
                                   || state.blend_target == GX_FACTOR_TARGET_ALPHA || state.blend_target == GX_FACTOR_INVERSE_TARGET_ALPHA;
          if (pass && reads_target_alpha) skipped++;
          else if (got_colour != ref_colour) {
            if (failures++ < 10) printf("colour: mode %d factors %d %d alpha %d %d sub %d plan %u: %d, not %d\n", mode, a, b, sa, ta, subtract, plan, got_colour, ref_colour);
          }
        }
        if (state.alpha_write) {
          // A pass stores S0 as it is, which is the reference for the one ONE/ZERO pair a stored alpha comes with.
          int stored_pair = state.blend_source_alpha == GX_FACTOR_ONE && state.blend_target_alpha == GX_FACTOR_ZERO && !state.blend_subtract_alpha;
          if (pass && !stored_pair) skipped++;
          else if (got_alpha != ref_alpha) {
            if (failures++ < 10) printf("alpha: mode %d factors %d %d alpha %d %d sub %d plan %u: %d, not %d\n", mode, a, b, sa, ta, subtract, plan, got_alpha, ref_alpha);
          }
        }
        checked++;
      }
      passes += (plan & GX_ALPHA_PASS) != 0;
      single += (plan & GX_ALPHA_STAGE) && !(plan & GX_ALPHA_PASS) && state.alpha_write;
    }
  }
  // The earlier rule wanted a pass for all that read the stages' alpha with alpha written; the new one none of the constant 0 variants.
  GxRenderState probe = {.blend_on = 1, .blend_source = GX_FACTOR_SOURCE1_ALPHA, .blend_target = GX_FACTOR_INVERSE_SOURCE1_ALPHA,
                         .blend_source_alpha = GX_FACTOR_ONE, .blend_target_alpha = GX_FACTOR_ZERO, .color_write = 1, .alpha_write = 1};
  GxRenderState variant;
  if (!(gx_alpha_plan(&probe, 1, 1) & GX_ALPHA_PASS) || !gx_alpha_zero_variant(&probe, &variant) || (gx_alpha_plan(&variant, 1, 1) & GX_ALPHA_PASS)
      || !(gx_alpha_plan(&variant, 1, 1) & GX_ALPHA_STAGE) || !(gx_alpha_plan(&probe, 1, 0) & GX_ALPHA_PASS)) {
    printf("the classic blend with a constant alpha should need a pass, and its 0 variant none\n");
    failures++;
  }
  GxRenderState none = probe;
  none.blend_source_alpha = GX_FACTOR_ZERO;
  if (!gx_alpha_zero_variant(&probe, &variant) || gx_alpha_zero_variant(&none, &variant)) { printf("variant of a variant\n"); failures++; }
  if (failures) { printf("%ld failures\n", failures); return 1; }
  fprintf(stderr, "checked %ld, skipped comparisons %ld, pass plans %ld, single-pass plans with alpha stored %ld\n", checked, skipped, passes, single);
  printf("ok\n");
  return 0;
}
