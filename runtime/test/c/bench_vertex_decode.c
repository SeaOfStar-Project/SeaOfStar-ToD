// Times the vertex decoders (gx_vertex.c) on made-up data: ns per vertex for the fused, planned and
// general decoders, for the formats the town scene sends (indexed positions, normals, colors and
// coordinates) and for the same parts sent in the stream. Not run by the test suite.
//   clang -O2 -fwrapv -ffp-contract=off -fsigned-char -Irecomp/runtime -Itest test/c/bench_vertex_decode.c -lm -o bench
#include "../../recomp/runtime/gx_vertex.c"
#include "harness.h"

GxState gx;
unsigned gx_cp_written = 1;
void debug_unsupported(const char *id, const char *what, uint32_t value) { (void)id, (void)what, (void)value; }
int watch_enabled(void) { return 0; }
uint64_t watch_range(uint32_t address, uint32_t bytes) { (void)address, (void)bytes; return WATCH_NONE; }
uint64_t watch_count(uint32_t address, uint32_t bytes) { (void)address, (void)bytes; return WATCH_NONE; }

static uint32_t seed = 777;
static uint32_t next(void) { seed ^= seed << 13, seed ^= seed >> 17, seed ^= seed << 5; return seed; }
static double seconds(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (double)t.tv_sec + t.tv_nsec / 1e9; }

enum { MOST_COUNT = 4096 };
static Vertex out[MOST_COUNT];
static int COUNT = 48, REPEATS = 200000;

static void run(const char *name, int indexed, uint32_t low, uint32_t high, uint32_t a, uint32_t b, uint32_t c) {
  gx.cp[CP_DESCRIPTOR_LOW] = low, gx.cp[CP_DESCRIPTOR_HIGH] = high;
  gx.cp[CP_FORMAT_A] = a, gx.cp[CP_FORMAT_B] = b, gx.cp[CP_FORMAT_C] = c;
  gx.cp[CP_MATRIX_INDEX] = 0, gx.cp[CP_MATRIX_INDEX + 0x10] = 0;
  for (int i = 0; i < ARRAYS; i++) gx.cp[CP_ARRAY_BASE + i] = 0x00100000u + (uint32_t)i * 0x40000u, gx.cp[CP_ARRAY_STRIDE + i] = 16;
  gx_cp_written++;
  static uint8_t stream[MOST_COUNT * 64];
  for (size_t i = 0; i < sizeof stream; i++) stream[i] = (uint8_t)next();
  if (indexed) for (size_t i = 0; i + 1 < sizeof stream; i += 2) stream[i] = 0, stream[i + 1] = (uint8_t)(next() % 200);
  double fused_ns, planned_ns, general_ns;
  const Plan *plan = plan_for(0);
  fused_off = 0;
  double t = seconds();
  for (int r = 0; r < REPEATS; r++) gx_read_vertices(stream, 0, COUNT, out);
  fused_ns = (seconds() - t) / REPEATS / COUNT * 1e9;
  fused_off = 1;
  t = seconds();
  for (int r = 0; r < REPEATS; r++) gx_read_vertices(stream, 0, COUNT, out);
  planned_ns = (seconds() - t) / REPEATS / COUNT * 1e9;
  t = seconds();
  for (int r = 0; r < REPEATS / 4; r++) read_vertices_general(stream, 0, COUNT, out);
  general_ns = (seconds() - t) / (REPEATS / 4) / COUNT * 1e9;
  printf("%-34s fused %6.1f   planned %6.1f   general %6.1f ns/vertex   (fusable %d, %.2fx vs planned)\n", name, fused_ns, planned_ns, general_ns, plan->fusable, planned_ns / fused_ns);
}

int main(void) {
  memory_start();
  for (uint32_t at = 0; at < 0x400000; at += 4) memcpy(host(at), &(uint32_t){next()}, 4);
  // Descriptor: position, normal, color 0 and texture 0 as given; formats: A bit 0 xyz, 1-3 type, 4-8 shift, 9 normal 9, 10-12 normal type,
  // 13 alpha, 14-16 color type, 21 uv count, 22-24 uv type, 25-29 shift; bit 30 dequantize.
  uint32_t a_s16 = 1u << 30 | 1 | (3u << 1) | (8u << 4) | (1u << 10) | (1u << 13) | (5u << 14) | (1u << 21) | (2u << 22) | (15u << 25);
  static const int sizes[] = {3, 4, 8, 48, 500, 4000};
  for (unsigned s = 0; s < sizeof sizes / sizeof *sizes; s++) {
    COUNT = sizes[s], REPEATS = 4000000 / COUNT;
    printf("-- %d vertices a draw\n", COUNT);
    run("indexed s16x3 s8x3 rgba8 u16x2", 1, 0x1 << 9 | 3u << 9 | 3u << 11 | 3u << 13, 3, a_s16, 0, 0);
    run("direct  s16x3 s8x3 rgba8 u16x2", 0, 1u << 9 | 1u << 11 | 1u << 13, 1, a_s16, 0, 0);
  }
  COUNT = 48, REPEATS = 200000;
  run("indexed s16x3 s8x3 rgba8 u16x2", 1, 0x1 << 9 | 3u << 9 | 3u << 11 | 3u << 13, 3, a_s16, 0, 0);
  run("direct  s16x3 s8x3 rgba8 u16x2", 0, 1u << 9 | 1u << 11 | 1u << 13, 1, a_s16, 0, 0);
  run("indexed16 position only", 1, 3u << 9, 0, a_s16, 0, 0);
  run("direct float x3 + float uv", 0, 1u << 9 | 0, 1, 1 | (4u << 1) | (1u << 21) | (4u << 22), 0, 0);
  return 0;
}
