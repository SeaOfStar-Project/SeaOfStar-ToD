// A thread's sleep that another thread can cut short, for threads that wait on each other's work
// (the commands thread for commands, the game's thread for a signal from it). Linux and Android:
// a futex on a word the waker sets, with the sleeper's flag so a wake that finds nobody
// asleep costs no call to the kernel. Elsewhere HOST_WAIT_FUTEX is undefined and callers keep
// their timed sleeps.
//
// A wake that finds nobody asleep leaves the word set, so the next wait returns at once and
// clears it. Waits can therefore end early for no reason: callers check what they wait for, and
// wait again.
#pragma once
#include <stdatomic.h>
#include <stdint.h>
#ifdef __linux__
#include <linux/futex.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#define HOST_WAIT_FUTEX 1

typedef struct { _Atomic int word, asleep; } Sleeper;

// Sleeps for at most `nanoseconds`, or until sleeper_wake. A wake sent between the caller's last
// look at what it waits for and this call is not lost: it leaves the word set, and the exchange
// below sees it (both sides use sequentially consistent operations, so one of them sees the other).
static inline void sleeper_wait(Sleeper *sleeper, uint64_t nanoseconds) {
  struct timespec limit = {(time_t)(nanoseconds / 1000000000u), (long)(nanoseconds % 1000000000u)};
  atomic_store_explicit(&sleeper->asleep, 1, memory_order_seq_cst);
  if (!atomic_exchange_explicit(&sleeper->word, 0, memory_order_seq_cst)) syscall(SYS_futex, &sleeper->word, FUTEX_WAIT_PRIVATE, 0, &limit, 0, 0);
  atomic_store_explicit(&sleeper->asleep, 0, memory_order_seq_cst);
}

static inline void sleeper_wake(Sleeper *sleeper) {
  atomic_store_explicit(&sleeper->word, 1, memory_order_seq_cst);
  if (atomic_load_explicit(&sleeper->asleep, memory_order_seq_cst)) syscall(SYS_futex, &sleeper->word, FUTEX_WAKE_PRIVATE, 1, 0, 0, 0);
}
#endif
