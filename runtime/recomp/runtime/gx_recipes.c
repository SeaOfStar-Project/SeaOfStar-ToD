// What a title has drawn with, kept so that later runs build it all before the first
// picture, instead of during play, where each new shader holds up a frame. It's kept as
// recipes (gx_backend.h), the keys shaders are written from, rather than compiled code,
// so it suits any computer and survives changes to how shaders are written. The file is
// shaders.bin in the title's data folder (profile.h); add_game.py carries it in the disc
// image too, so a game's first run on another computer or console starts with it.
//
// The file is a header, then one GxRecipe after another. It only ever grows by whole
// records written at the end, so a run killed part way leaves at most one torn record,
// which loading stops at. Recipes of another size, version or title are left alone.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "drive.h"
#include "gx_backend.h"
#include "profile.h"
#include "title.h"

// A new version when a key's meaning changes. Old recipes would only build shaders
// nothing draws with, so that's a waste rather than a fault. Version 2 added the
// fragment key's bounding-box bit; version 1 records read as though it were off.
enum { RECIPE_VERSION = 2, LEGACY_RECIPE_VERSION = 1, FIRST_SLOTS = 256 };
typedef struct { char magic[4]; uint32_t version, record_bytes, title_high, title_low, unused[3]; } RecipeHeader;
// What version 1 wrote: the same fields, without the bounding-box bit.
typedef struct { TevKey tev; VertexKey vertex; GxRenderState state; } LegacyRecipe;

static GxRecipe *recipes;
static unsigned recipe_count, recipe_capacity;
static unsigned *slots;  // Open addressing from a recipe's hash to its index, plus 1.
static unsigned slot_count;
static FILE *file;

static uint32_t hash_recipe(const GxRecipe *recipe) {
  const uint8_t *bytes = (const uint8_t *)recipe;
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < sizeof *recipe; i++) hash = (hash ^ bytes[i]) * 16777619u;
  return hash;
}

static unsigned *slot_of(const GxRecipe *recipe) {
  unsigned slot = hash_recipe(recipe) & (slot_count - 1);
  while (slots[slot] && memcmp(&recipes[slots[slot] - 1], recipe, sizeof *recipe)) slot = (slot + 1) & (slot_count - 1);
  return &slots[slot];
}

// Returns 1 if it's new.
static int keep(const GxRecipe *recipe) {
  if ((recipe_count + 1) * 2 > slot_count) {
    free(slots);
    slot_count = slot_count ? slot_count * 2 : FIRST_SLOTS;
    slots = calloc(slot_count, sizeof *slots);
    for (unsigned i = 0; i < recipe_count; i++) *slot_of(&recipes[i]) = i + 1;
  }
  unsigned *slot = slot_of(recipe);
  if (*slot) return 0;
  if (recipe_count == recipe_capacity) {
    recipe_capacity = recipe_capacity ? recipe_capacity * 2 : FIRST_SLOTS;
    recipes = realloc(recipes, recipe_capacity * sizeof *recipes);
  }
  recipes[recipe_count++] = *recipe;
  *slot = recipe_count;
  return 1;
}

static RecipeHeader own_header(void) {
  RecipeHeader header = {{'W', 'R', 'S', 'R'}, RECIPE_VERSION, sizeof(GxRecipe), title->title_high, title->title_low, {0}};
  return header;
}

// Returns how many were new, or -1 if they're for something else. A torn last record
// counts as something else too, so the file is written again without it. Sets legacy
// when the records are version 1's, so the caller writes them again in this version's
// form rather than appending to a file whose header still says version 1.
static int keep_all(const uint8_t *data, size_t bytes, int *legacy) {
  RecipeHeader header = own_header();
  if (bytes < sizeof header) return -1;
  RecipeHeader found;
  memcpy(&found, data, sizeof found);
  if (memcmp(found.magic, header.magic, sizeof header.magic) || found.title_high != header.title_high
      || found.title_low != header.title_low) return -1;
  size_t record_bytes;
  if (found.version == RECIPE_VERSION && found.record_bytes == sizeof(GxRecipe)) {
    record_bytes = sizeof(GxRecipe);
  } else if (found.version == LEGACY_RECIPE_VERSION && found.record_bytes == sizeof(LegacyRecipe)) {
    record_bytes = sizeof(LegacyRecipe), *legacy = 1;
  } else {
    return -1;
  }
  int added = (bytes - sizeof header) % record_bytes ? -1 : 0;
  for (size_t at = sizeof header; at + record_bytes <= bytes; at += record_bytes) {
    GxRecipe recipe;
    memset(&recipe, 0, sizeof recipe);
    memcpy(&recipe, data + at, record_bytes);  // The fields version 1 kept come first.
    if (keep(&recipe) && added >= 0) added++;
  }
  return added;
}

static uint8_t *read_file(const char *path, size_t *bytes) {
  FILE *in = fopen(path, "rb");
  if (!in) return 0;
  fseek(in, 0, SEEK_END);
  long size = ftell(in);
  fseek(in, 0, SEEK_SET);
  uint8_t *data = size > 0 ? malloc((size_t)size) : 0;
  *bytes = data && fread(data, 1, (size_t)size, in) == (size_t)size ? (size_t)size : 0;
  fclose(in);
  return data;
}

// Writes every recipe known, the disc's ones included, so the file holds them all.
static FILE *write_fresh(const char *path) {
  char partial[4200];
  snprintf(partial, sizeof partial, "%s.partial", path);
  FILE *out = fopen(partial, "wb");
  RecipeHeader header = own_header();
  int ok = out && fwrite(&header, sizeof header, 1, out) == 1 && fwrite(recipes, sizeof *recipes, recipe_count, out) == recipe_count;
  if (out && fclose(out)) ok = 0;
  if (!ok || rename(partial, path)) {
    fprintf(stderr, "shader recipes: can't write %s\n", path);
    return 0;
  }
  return fopen(path, "ab");
}

static double seconds_now(void) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (double)now.tv_sec + (double)now.tv_nsec / 1e9;
}

void gx_recipes_start(void) {
  if (getenv("WII_RECOMP_NO_SHADER_CACHE") || !title) return;
  char path[4096];
  if (!title_data_path("shaders.bin", path, sizeof path)) return;
  size_t bytes = 0, carried_bytes = 0;
  uint8_t *data = read_file(path, &bytes);
  int own_legacy = 0;
  int own = data ? keep_all(data, bytes, &own_legacy) : -1;
  free(data);
  uint8_t *carried = drive_shaders(&carried_bytes);
  int disc_legacy = 0;
  int from_disc = carried ? keep_all(carried, carried_bytes, &disc_legacy) : -1;
  free(carried);
  // Version 1's records, or a disc's, are written into this version's file whole.
  file = own >= 0 && !own_legacy && from_disc <= 0 && !disc_legacy ? fopen(path, "ab") : write_fresh(path);
  if (!recipe_count) return;
  double start = seconds_now();
  gx_shader_prepare(recipes, recipe_count);
  fprintf(stderr, "shader recipes: %u ready in %.0f ms (%d from the disc)\n", recipe_count, (seconds_now() - start) * 1000,
          from_disc > 0 ? from_disc : 0);
}

void gx_recipe_note(GxShader shader, const GxRenderState *state) {
  GxRecipe recipe;
  if (!file || !gx_shader_recipe(shader, state, &recipe) || !keep(&recipe)) return;
  fwrite(&recipe, sizeof recipe, 1, file);
  fflush(file);
}
