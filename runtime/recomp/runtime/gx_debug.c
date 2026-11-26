// WII_RECOMP_GX_DEBUG: see gx_debug.h.
#include "gx_debug.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const known[] = {"nodual", "oldnodual", "fullalphapass", "nobbox", "nopush", "noaniso", "nodepthlinear", "depthalways", "nocull", "noblend",
                                    "depth", "nostrip", "keepdepth", "depthevery", "noflat", "noflatdepth", "noearlydepth", "noside", "keepempty",
                                    "noearlyread", "drawclears", "keepcopyback", "copiesoncard", "report"};

static pthread_once_t parsed_once = PTHREAD_ONCE_INIT;
static char *list;  // The variable's text, with commas made into ends of strings.

static void parse(void) {
  const char *text = getenv("WII_RECOMP_GX_DEBUG");
  if (!text || !*text) return;
  list = strdup(text);
  for (char *at = list; *at; at++) if (*at == ',') *at = 0;
  for (const char *name = list; name < list + strlen(text) + 1; name += strlen(name) + 1) {
    if (!*name) continue;
    size_t length = strcspn(name, "=");
    int found = 0;
    for (size_t i = 0; i < sizeof known / sizeof known[0]; i++) found |= strlen(known[i]) == length && !strncmp(known[i], name, length);
    if (!found) fprintf(stderr, "gx debug: %s is not a switch (see gx_debug.h)\n", name);
  }
}

// The entry for name (an exact name, or name=value), or 0.
static const char *entry(const char *name) {
  pthread_once(&parsed_once, parse);
  if (!list) return 0;
  size_t length = strlen(name), total = strlen(getenv("WII_RECOMP_GX_DEBUG"));
  for (const char *at = list; at < list + total + 1; at += strlen(at) + 1) {
    if (!strncmp(at, name, length) && (!at[length] || at[length] == '=')) return at;
  }
  return 0;
}

static void say_once(const char *name, const char *found) {
  static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
  static char told[32][24];
  pthread_mutex_lock(&lock);
  for (int i = 0; i < 32; i++) {
    if (!strcmp(told[i], name)) goto done;
    if (!told[i][0]) {
      snprintf(told[i], sizeof told[i], "%s", name);
      fprintf(stderr, "gx debug: %s on\n", found);
      goto done;
    }
  }
done:
  pthread_mutex_unlock(&lock);
}

int gx_debug(const char *name) {
  const char *found = entry(name);
  if (!found) return 0;
  say_once(name, found);
  return 1;
}

const char *gx_debug_value(const char *name) {
  const char *found = entry(name);
  if (!found || found[strlen(name)] != '=') return 0;
  say_once(name, found);
  return found + strlen(name) + 1;
}
