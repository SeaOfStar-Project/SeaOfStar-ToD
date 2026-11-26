// Core placement and the performance hint on Android (host_perf.h says what the switches are).
#ifdef __ANDROID__
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "host_perf.h"

#define MOST_CPUS 32

static cpu_set_t fastest;  // The cores to use, once worked out.
static int fastest_count, worked_out;
static int mode_off, mode_split, mode_auto;
static cpu_set_t listed;
static int listed_count;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static long read_number(const char *format, int cpu) {
  char path[128];
  snprintf(path, sizeof path, format, cpu);
  FILE *file = fopen(path, "r");
  if (!file) return -1;
  long value = -1;
  if (fscanf(file, "%ld", &value) != 1) value = -1;
  fclose(file);
  return value;
}

// How fast each core can go: its top frequency, or if the phone won't say, the kernel's capacity number.
static const char *const SOURCES[] = {"/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_max_freq",
                                      "/sys/devices/system/cpu/cpu%d/cpu_capacity"};

static int parse_list(const char *text, cpu_set_t *set) {
  int count = 0;
  CPU_ZERO(set);
  while (*text) {
    char *end;
    long first = strtol(text, &end, 10), last = first;
    if (end == text) return 0;
    if (*end == '-') { text = end + 1; last = strtol(text, &end, 10); }
    for (long cpu = first; cpu <= last && cpu < MOST_CPUS; cpu++) if (!CPU_ISSET((int)cpu, set)) { CPU_SET((int)cpu, set); count++; }
    text = *end == ',' ? end + 1 : end;
    if (*end && *end != ',') return 0;
  }
  return count;
}

static void describe(const cpu_set_t *set, char *out, size_t size) {
  out[0] = 0;
  for (int cpu = 0, used = 0; cpu < MOST_CPUS; cpu++) {
    if (CPU_ISSET(cpu, set) && (size_t)used < size - 4) used += snprintf(out + used, size - (size_t)used, "%s%d", used ? "," : "", cpu);
  }
}

static void work_out(void) {
  if (worked_out) return;
  worked_out = 1;
  const char *asked = getenv("WII_RECOMP_AFFINITY");
  if (asked && !strcmp(asked, "off")) {
    mode_off = 1;
    fprintf(stderr, "perf: core placement is off, as WII_RECOMP_AFFINITY asked\n");
    return;
  }
  if (asked && !strcmp(asked, "split")) mode_split = 1;
  else if (!asked || !*asked || !strcmp(asked, "auto")) mode_auto = 1;
  else {
    listed_count = parse_list(asked, &listed);
    if (!listed_count) fprintf(stderr, "perf: WII_RECOMP_AFFINITY=%s isn't off, auto, split or a list of cores such as 6,7: using auto\n", asked);
  }
  long speed[MOST_CPUS];
  int cpus = (int)sysconf(_SC_NPROCESSORS_CONF);
  if (cpus > MOST_CPUS) cpus = MOST_CPUS;
  int known = 0;
  long top = -1, next = -1;
  char summary[256] = "";
  for (int cpu = 0; cpu < cpus; cpu++) {
    speed[cpu] = -1;
    for (size_t i = 0; i < sizeof SOURCES / sizeof *SOURCES && speed[cpu] < 0; i++) speed[cpu] = read_number(SOURCES[i], cpu);
    if (speed[cpu] >= 0) known++;
    size_t at = strlen(summary);
    snprintf(summary + at, sizeof summary - at, "%scpu%d=%ld", at ? " " : "", cpu, speed[cpu]);
    if (speed[cpu] > top) { next = top; top = speed[cpu]; }
    else if (speed[cpu] < top && speed[cpu] > next) next = speed[cpu];
  }
  fprintf(stderr, "perf: %d cores, speeds (max kHz or capacity): %s\n", cpus, summary);
  if (known < cpus || next < 0) {
    fprintf(stderr, "perf: %s, so the scheduler places the threads\n", known < cpus ? "the phone doesn't say how fast every core is" : "all cores are alike");
    return;
  }
  CPU_ZERO(&fastest);
  for (int cpu = 0; cpu < cpus; cpu++) {
    if (speed[cpu] == top) { CPU_SET(cpu, &fastest); fastest_count++; }
  }
  if (fastest_count < 2) {  // A single prime core isn't room for two busy threads: the next tier joins it.
    for (int cpu = 0; cpu < cpus; cpu++) {
      if (speed[cpu] == next) { CPU_SET(cpu, &fastest); fastest_count++; }
    }
  }
  describe(&fastest, summary, sizeof summary);
  fprintf(stderr, "perf: the fastest cores are %s\n", summary);
  // Measured on a Galaxy M33 (two fast cores): both busy threads on the pair made the frame slower than
  // leaving them to the scheduler, one thread per core was best. With exactly two fastest cores that is the default.
  if (mode_auto && !listed_count && fastest_count == 2) {
    mode_split = 1;
    fprintf(stderr, "perf: exactly two fastest cores, so one thread goes on each (split)\n");
  }
}

// ---- the performance hint (API 33; looked up at run time so older phones go without) ----

typedef struct APerformanceHintManager APerformanceHintManager;
typedef struct APerformanceHintSession APerformanceHintSession;
static APerformanceHintSession *(*create_session)(APerformanceHintManager *, const int32_t *, size_t, int64_t);
static int (*report_duration)(APerformanceHintSession *, int64_t);
static APerformanceHintSession *session;
static unsigned reports, report_failures;

static void start_hint(void) {
  const char *asked = getenv("WII_RECOMP_PERF_HINT");
  if (asked && !strcmp(asked, "off")) {
    fprintf(stderr, "perf: the performance hint is off, as WII_RECOMP_PERF_HINT asked\n");
    return;
  }
  void *android = dlopen("libandroid.so", RTLD_NOW);
  APerformanceHintManager *(*get_manager)(void) = android ? (APerformanceHintManager *(*)(void))dlsym(android, "APerformanceHint_getManager") : 0;
  create_session = android ? (APerformanceHintSession *(*)(APerformanceHintManager *, const int32_t *, size_t, int64_t))dlsym(android, "APerformanceHint_createSession") : 0;
  report_duration = android ? (int (*)(APerformanceHintSession *, int64_t))dlsym(android, "APerformanceHint_reportActualWorkDuration") : 0;
  if (!get_manager || !create_session || !report_duration) {
    fprintf(stderr, "perf: no performance hint on this Android (it needs API 33)\n");
    return;
  }
  APerformanceHintManager *manager = get_manager();
  if (!manager) {
    fprintf(stderr, "perf: the system has no performance hint manager\n");
    return;
  }
  double target_ms = 33.3;
  const char *target = getenv("WII_RECOMP_HINT_TARGET_MS");
  if (target && atof(target) > 1) target_ms = atof(target);
  int32_t threads[1] = {(int32_t)syscall(SYS_gettid)};
  char task[64];
  snprintf(task, sizeof task, "/proc/self/task/%d", (int)threads[0]);
  int own_thread = access(task, F_OK) == 0;  // The service only takes thread ids of the calling process.
  errno = 0;
  session = create_session(manager, threads, 1, (int64_t)(target_ms * 1e6));
  int why = errno;
  fprintf(stderr, "perf: performance hint session for the game's thread %d (process %d, %s), target %.1f ms: %s\n", threads[0], (int)getpid(),
          own_thread ? "a thread of this process" : "NOT a thread of this process", target_ms, session ? "created" : "refused");
  if (!session) {
    // The NDK call returns only a null pointer. The thread id is the caller's own, so the refusal comes from the system:
    // no performance hint service or hardware support on this phone (Samsung builds often have none), or the app isn't in the foreground.
    fprintf(stderr, "perf: the hint was refused (errno %d: %s); the NDK gives no other reason, see logcat tags PerformanceHint and HintManager\n", why, why ? strerror(why) : "none set");
  }
}

void host_perf_frame(uint64_t work_nanoseconds) {
  if (!session || !report_duration) return;
  int result = report_duration(session, (int64_t)(work_nanoseconds ? work_nanoseconds : 1));
  reports++;
  if (result) report_failures++;
  if (reports == 1 || reports == 300) {
    fprintf(stderr, "perf: performance hint: %u frames reported, the last work %.1f ms, %u refused\n", reports, (double)work_nanoseconds / 1e6, report_failures);
  }
}

static void note_thread(int role);

void host_perf_place(int role) {
  note_thread(role);
  pthread_mutex_lock(&lock);
  work_out();
  if (role == HOST_PERF_GAME) start_hint();
  const cpu_set_t *mask = 0;
  int count = 0;
  if (!mode_off) {
    if (listed_count) { mask = &listed; count = listed_count; }
    else if (fastest_count) { mask = &fastest; count = fastest_count; }
  }
  cpu_set_t one;
  if (mask && mode_split && !listed_count && count >= 2) {
    int wanted = role == HOST_PERF_GAME ? 0 : 1, seen = 0, chosen = -1;
    for (int cpu = MOST_CPUS - 1; cpu >= 0 && chosen < 0; cpu--) {
      if (CPU_ISSET(cpu, mask) && seen++ == wanted) chosen = cpu;
    }
    if (chosen >= 0) { CPU_ZERO(&one); CPU_SET(chosen, &one); mask = &one; count = 1; }
  }
  pthread_mutex_unlock(&lock);
  if (!mask) return;
  char cores[128];
  describe(mask, cores, sizeof cores);
  if (sched_setaffinity(0, sizeof(cpu_set_t), mask)) {
    fprintf(stderr, "perf: can't put the %s thread (%d) on cores %s: %s\n", role == HOST_PERF_GAME ? "game" : "graphics", (int)syscall(SYS_gettid), cores, strerror(errno));
  } else {
    fprintf(stderr, "perf: the %s thread (%d) runs on cores %s\n", role == HOST_PERF_GAME ? "game" : "graphics", (int)syscall(SYS_gettid), cores);
  }
}
#endif

#ifdef __linux__
#ifndef __ANDROID__
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>
#include "host_perf.h"
#endif
#ifdef __ANDROID__
#include <fcntl.h>
#endif

// Where the game's and graphics threads are, and what the cores and the GPU are clocked at. Files are read with
// open and read only, a few small ones once per frames line (ten seconds), and the ones this system won't let us read
// are named once at the start.
static volatile int thread_ids[2];
static const char *const GPU_CLOCK = "/sys/kernel/gpu/gpu_clock", *const GPU_MAX_CLOCK = "/sys/kernel/gpu/gpu_max_clock";

static long read_file_number(const char *path) {
  int fd = open(path, O_RDONLY);
  if (fd < 0) return -1;
  char text[48];
  ssize_t got = read(fd, text, sizeof text - 1);
  close(fd);
  if (got <= 0) return -1;
  text[got] = 0;
  return strtol(text, 0, 10);
}

static long cpu_clock(const char *name, int cpu) {
  char path[96];
  snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu%d/cpufreq/%s", cpu, name);
  return read_file_number(path);
}

// The core a thread last ran on: field 39 of its stat line, counted from the fields after the name's closing bracket.
static int thread_core(int tid) {
  if (tid <= 0) return -1;
  char path[64], text[1024];
  snprintf(path, sizeof path, "/proc/self/task/%d/stat", tid);
  int fd = open(path, O_RDONLY);
  if (fd < 0) return -1;
  ssize_t got = read(fd, text, sizeof text - 1);
  close(fd);
  if (got <= 0) return -1;
  text[got] = 0;
  const char *at = strrchr(text, ')');
  if (!at) return -1;
  at++;
  for (int field = 2; field < 38 && at; field++) {  // Skip to field 39 (the one after is 40).
    at = strchr(at + 1, ' ');
  }
  return at ? atoi(at + 1) : -1;
}

static void note_thread(int role) {
  thread_ids[role] = (int)syscall(SYS_gettid);
  if (role != 0) return;
  // The start-up line: which clock files this system keeps from us.
  char missing[160] = "";
  int used = 0;
  struct { const char *name; long value; } files[] = {
    {"cpu0 scaling_cur_freq", cpu_clock("scaling_cur_freq", 0)}, {"cpu0 scaling_max_freq", cpu_clock("scaling_max_freq", 0)},
    {"gpu_clock", read_file_number(GPU_CLOCK)}, {"gpu_max_clock", read_file_number(GPU_MAX_CLOCK)},
  };
  for (size_t i = 0; i < sizeof files / sizeof *files; i++) {
    if (files[i].value < 0 && (size_t)used < sizeof missing - 40) used += snprintf(missing + used, sizeof missing - (size_t)used, "%s%s", used ? ", " : "", files[i].name);
  }
  if (used) fprintf(stderr, "perf: clock files not readable (left out of the frames line): %s\n", missing);
  else fprintf(stderr, "perf: clock files readable (cpu frequencies and the GPU clock go on the frames line)\n");
}

#ifndef __ANDROID__
void host_perf_place(int role) { note_thread(role); }
#endif

void host_perf_clocks(char *out, size_t size) {
  size_t used = 0;
  out[0] = 0;
  static const char *const NAMES[2] = {"game", "gx"};
  for (int role = 0; role < 2; role++) {
    int cpu = thread_core(thread_ids[role]);
    if (cpu < 0) continue;
    long now = cpu_clock("scaling_cur_freq", cpu), top = cpu_clock("scaling_max_freq", cpu);
    if (now < 0 && top < 0) continue;
    if (used < size) used += (size_t)snprintf(out + used, size - used, ", %s cpu%d %ld/%ld kHz", NAMES[role], cpu, now, top);
  }
  long gpu = read_file_number(GPU_CLOCK), gpu_top = read_file_number(GPU_MAX_CLOCK);
  if ((gpu >= 0 || gpu_top >= 0) && used < size) snprintf(out + used, size - used, ", gpu %ld/%ld", gpu, gpu_top);
}
#endif
