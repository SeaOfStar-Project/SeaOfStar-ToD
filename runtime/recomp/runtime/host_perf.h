// Where the game's threads run and how the phone is told what they need (host_perf.c, Android only; the
// calls do nothing elsewhere).
//
//   WII_RECOMP_AFFINITY   auto (the default): the game's thread and the commands thread go to the phone's fastest
//                         cores (the highest maximum frequency, from sysfs), and the rest are left to the scheduler;
//                         when there are exactly two fastest cores each thread gets one of them, as split does (measured
//                         best on a Galaxy M33; with more or fewer cores both threads share the whole set);
//                         off: nothing is changed; split: the game's thread takes the fastest cores' highest
//                         number, the commands thread the next one down; a list such as 6,7 or 4-7 pins both there.
//                         A phone whose cores are all alike, or that won't say, is left alone.
//   WII_RECOMP_PERF_HINT  off: no performance hint session. WII_RECOMP_HINT_TARGET_MS sets the frame target
//                         (33.3 by default).
#pragma once
#include <stddef.h>
#include <stdint.h>
enum { HOST_PERF_GAME, HOST_PERF_GRAPHICS };
#ifdef __ANDROID__
void host_perf_place(int role);       // Called by the thread itself, as it starts.
void host_perf_frame(uint64_t work_nanoseconds);  // One frame's work on the game's thread, with its waits taken out.
#else
static inline void host_perf_frame(uint64_t work_nanoseconds) { (void)work_nanoseconds; }
#ifdef __linux__
void host_perf_place(int role);       // Only notes the thread, for the clock report below.
#else
static inline void host_perf_place(int role) { (void)role; }
#endif
#endif
// Linux and Android: writes where the game's and graphics threads last ran and those cores' current and top clocks
// (kHz), and the Samsung GPU clock files when readable, for the ten-second frames line. Empty elsewhere.
#ifdef __linux__
void host_perf_clocks(char *out, size_t size);
#else
static inline void host_perf_clocks(char *out, size_t size) { if (size) out[0] = 0; }
#endif
