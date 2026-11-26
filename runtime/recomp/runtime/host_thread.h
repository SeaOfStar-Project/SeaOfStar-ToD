// Names for the runtime's own threads, so a thread list (top -H, a debugger, a profiler) says what each
// one is. A name is at most 15 characters. Each thread names itself from its own start function.
// Threads made from another one start with its name, which is why an Android process shows the app's
// "SDLThread" three times without this.
#pragma once
#ifdef _WIN32
void host_thread_name(const char *name);  // SetThreadDescription (host_thread.c).
static inline void host_thread_name_unless_main(const char *name) { (void)name; }
#else
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/prctl.h>
#include <sys/syscall.h>
#endif

static inline void host_thread_name(const char *name) {
#ifdef __linux__
  char shortened[16];
  strncpy(shortened, name, sizeof shortened - 1);
  shortened[sizeof shortened - 1] = 0;
  prctl(PR_SET_NAME, shortened, 0, 0, 0);
#elif defined(__APPLE__)
  pthread_setname_np(name);
#else
  (void)name;
#endif
}

// For the thread a program starts on: on a desktop Linux its name is the program's (what ps shows), which
// stays. Android's SDL runs the game's entry on a thread of its own (not the process's first), so it is named.
static inline void host_thread_name_unless_main(const char *name) {
#ifdef __linux__
  if ((pid_t)syscall(SYS_gettid) == getpid()) return;
#endif
  host_thread_name(name);
}
#endif
