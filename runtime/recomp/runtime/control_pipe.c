// The control pipe (WII_RECOMP_CONTROL): a named pipe that takes one command a line, so
// a script or another program can play. The controls' commands are in input.c; the
// console's are here: reset and power to press its buttons, shot <file> to save the next
// picture, record <frames> <file> to record frames for recomp/tools/replay, record-sound
// <frames> <file> to record the audio processor's work for recomp/tools/sound_bench.py,
// timeline <frames> <file> to record what each thread does (timeline.h), dump and skip for
// the graphics inspector, peek <hex address> <words> to print memory,
// state to print the interrupt and IPC state, eject to empty the drive, and insert
// <image> to put a disc in (in a console, the name of an image on its shelf).
//
// Replies. On Windows the pipe is two-way. A sender that sends "reply on" first gets one
// line back for every command it sends after: "ok ..." or "error ...", never more than one
// line, so reading it can't lose its place. Without "reply on" nothing is written back,
// and everything is still printed on stderr as before. "reply off" turns it off again.
//
// Memory and calls from outside: poke <addr> <1|2|4> <value> writes guest memory, pokef <addr>
// <float> writes a big-endian 32-bit float, and call <addr> [r3 .. r10] (up to 8 arguments)
// runs a translated guest function, all numbers in hex but the float. Both are
// queued and done on the game's thread at its next interrupt check (control_service, which
// poll_interrupts calls), and the reply comes once they are done.
#include <fcntl.h>
#include <stdarg.h>
#include <time.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "console.h"
#include "host_thread.h"
#include "cpu.h"
#include "debug.h"
#include "dsp_record.h"
#include "gx_inspect.h"
#include "gx_shot.h"
#include "gx.h"
#include "hardware.h"
#include "input.h"
#include "timeline.h"
#include "pads.h"

static void pipe_write(const char *text);  // One reply line to the sender, if it asked for them (below).
static int reply_enabled, reply_sent;

static void reply(const char *format, ...) {
  char text[4096];
  va_list arguments;
  va_start(arguments, format);
  vsnprintf(text, sizeof text - 2, format, arguments);
  va_end(arguments);
  reply_sent = 1;
  if (reply_enabled) pipe_write(text);
}

// Prints guest memory as words, for looking into a program that seems stuck. The reply is
// the same words on one line.
static void peek(uint32_t address, int words) {
  char text[4096];
  size_t length = (size_t)snprintf(text, sizeof text, "ok peek %08x:", address);
  for (int i = 0; i < words && i < 256; i++) {
    uint32_t value = rd32((address + i * 4) | 0x80000000u);
    if (i % 4 == 0) fprintf(stderr, "%speek %08x:", i ? "\n" : "", address + i * 4);
    fprintf(stderr, " %08x", value);
    if (length < sizeof text - 12) length += (size_t)snprintf(text + length, sizeof text - length, " %08x", value);
  }
  fprintf(stderr, "\n");
  reply("%s", text);
}

// A write or a call for the game's thread. The control thread fills it in, sets state to
// QUEUED, and waits; the game's thread takes it (RUNNING), does it and sets DONE.
enum { REQUEST_NONE, REQUEST_POKE, REQUEST_CALL };
enum { STATE_IDLE, STATE_QUEUED, STATE_RUNNING, STATE_DONE, STATE_CANCELLED };
static struct {
  int kind;
  uint32_t address, width, value, arguments[8], result;
  char error[128];
  int state;
} request;
static int in_call;
enum { QUEUED_WAIT_MS = 5000, RUNNING_WAIT_MS = 10000 };

int control_call_active(void) { return in_call; }

// Only main memory: an address the way peek names it (0x80000000 or 0xc0000000 added, or
// not), inside MEM1 or MEM2. Hardware registers and the locked cache aren't poked.
static int poke_range_ok(uint32_t address, uint32_t width) {
  uint32_t physical = address & 0x3fffffffu;
  return (physical < MEM1_SIZE && physical + width <= MEM1_SIZE) ||
         (physical >= MEM2_START && physical - MEM2_START < MEM2_SIZE && physical - MEM2_START + width <= MEM2_SIZE);
}

static void do_poke(void) {
  uint32_t address = request.address | 0x80000000u, width = request.width, old, value = request.value;
  switch (width) {
    case 1: old = rd8(address); value &= 0xffu; wr8(address, value); break;
    case 2: old = rd16(address); value &= 0xffffu; wr16(address, value); break;
    default: old = rd32(address); wr32(address, value); break;
  }
  fprintf(stderr, "control: poke 0x%08x w=%u old=0x%0*x new=0x%0*x\n", address, width, (int)width * 2, old, (int)width * 2, value);
}

static void do_call(void) {
  uint32_t address = request.address;
  GuestFunction run = translated_at(address);
  if (!run) {
    snprintf(request.error, sizeof request.error, "no translated function starts at 0x%08x", address);
    fprintf(stderr, "control: call 0x%08x refused: %s\n", address, request.error);
    return;
  }
  // The whole processor state is kept and put back, so the game goes on as if nothing ran.
  // The call runs with interrupts off, as an interrupt handler does, so it can't be
  // interrupted into another thread, and below the stack pointer with room for a frame.
  static Cpu saved;
  saved = cpu;
  uint32_t sp = (saved.r[1] - 0x400u) & ~0xfu;
  wr32(sp, saved.r[1]);
  cpu.r[1] = sp;
  for (int i = 0; i < 8; i++) cpu.r[3 + i] = request.arguments[i];
  cpu.lr = 0;
  cpu.msr &= ~0x8000u;
  in_call = 1;
  run();
  in_call = 0;
  request.result = cpu.r[3];
  uint32_t fpscr_after = cpu.fpscr;
  cpu = saved;
  if ((fpscr_after ^ saved.fpscr) & (FPSCR_ROUNDING | FPSCR_NON_IEEE)) fpscr_mode_changed(saved.fpscr);
  fprintf(stderr, "control: call 0x%08x args 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x -> r3=0x%08x\n", address, request.arguments[0],
          request.arguments[1], request.arguments[2], request.arguments[3], request.arguments[4], request.arguments[5],
          request.arguments[6], request.arguments[7], request.result);
}

// On the game's thread, from poll_interrupts: the same places an interrupt is taken, so
// the processor's state is all in cpu. A call waits until interrupts are on, as an
// interrupt does, and never starts inside another call.
void control_service(void) {
  if (__atomic_load_n(&request.state, __ATOMIC_ACQUIRE) != STATE_QUEUED) return;
  if (request.kind == REQUEST_CALL && (in_call || !(cpu.msr & 0x8000u))) return;
  int expected = STATE_QUEUED;
  if (!__atomic_compare_exchange_n(&request.state, &expected, STATE_RUNNING, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return;
  request.error[0] = 0;
  if (request.kind == REQUEST_POKE) do_poke();
  else do_call();
  __atomic_store_n(&request.state, STATE_DONE, __ATOMIC_RELEASE);
}

static void wait_ms(int milliseconds) {
  struct timespec pause = {0, 1000000};
  for (int i = 0; i < milliseconds && __atomic_load_n(&request.state, __ATOMIC_ACQUIRE) != STATE_DONE; i++) nanosleep(&pause, 0);
}

// Hands the request to the game's thread and waits for it. Returns 1 once it is done.
static int run_on_game_thread(const char *what) {
  int current = __atomic_load_n(&request.state, __ATOMIC_ACQUIRE);
  if (current == STATE_RUNNING) {
    reply("error %s: the last request is still running on the game's thread", what);
    fprintf(stderr, "control: %s refused: the last request is still running\n", what);
    return 0;
  }
  __atomic_store_n(&request.state, STATE_QUEUED, __ATOMIC_RELEASE);
  wait_ms(QUEUED_WAIT_MS);
  int expected = STATE_QUEUED;
  if (__atomic_compare_exchange_n(&request.state, &expected, STATE_CANCELLED, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
    reply("error %s: not done in %d ms (the game did not reach an interrupt check%s)", what, QUEUED_WAIT_MS,
          request.kind == REQUEST_CALL ? " with interrupts on" : "");
    fprintf(stderr, "control: %s cancelled: the game's thread did not take it in %d ms\n", what, QUEUED_WAIT_MS);
    return 0;
  }
  wait_ms(RUNNING_WAIT_MS);  // Taken: let it finish.
  if (__atomic_load_n(&request.state, __ATOMIC_ACQUIRE) != STATE_DONE) {
    reply("error %s: started but did not return in %d ms (a guest function that waits or switches threads cannot be called)", what, RUNNING_WAIT_MS);
    fprintf(stderr, "control: %s did not return\n", what);
    return 0;
  }
  __atomic_store_n(&request.state, STATE_IDLE, __ATOMIC_RELEASE);
  return 1;
}

static void poke_command(const char *line) {
  unsigned address, width, value;
  char extra;
  if (sscanf(line, "poke %x %u %x %c", &address, &width, &value, &extra) != 3 || (width != 1 && width != 2 && width != 4)) {
    reply("error poke: usage: poke <addr-hex> <1|2|4> <value-hex>");
    fprintf(stderr, "control: poke refused: usage: poke <addr-hex> <1|2|4> <value-hex>\n");
    return;
  }
  if (!poke_range_ok(address, width)) {
    reply("error poke: 0x%08x is outside MEM1 and MEM2", address);
    fprintf(stderr, "control: poke 0x%08x w=%u refused: outside MEM1 and MEM2\n", address, width);
    return;
  }
  request.kind = REQUEST_POKE, request.address = address, request.width = width, request.value = value;
  if (run_on_game_thread("poke")) reply("ok poke %08x w=%u", address | 0x80000000u, width);
}

// pokef <addr> <float>: writes the float as a 32-bit big-endian IEEE single (same checks as poke 4).
static void pokef_command(const char *line) {
  unsigned address;
  float number;
  char extra;
  if (sscanf(line, "pokef %x %f %c", &address, &number, &extra) != 2) {
    reply("error pokef: usage: pokef <addr-hex> <float>");
    fprintf(stderr, "control: pokef refused: usage: pokef <addr-hex> <float>\n");
    return;
  }
  if (!poke_range_ok(address, 4)) {
    reply("error pokef: 0x%08x is outside MEM1 and MEM2", address);
    fprintf(stderr, "control: pokef 0x%08x refused: outside MEM1 and MEM2\n", address);
    return;
  }
  uint32_t bits;
  memcpy(&bits, &number, sizeof bits);
  fprintf(stderr, "control: pokef 0x%08x value=%g bits=0x%08x\n", address, (double)number, bits);
  request.kind = REQUEST_POKE, request.address = address, request.width = 4, request.value = bits;
  if (run_on_game_thread("pokef")) reply("ok pokef %08x %g", address | 0x80000000u, (double)number);
}

static void call_command(const char *line) {
  unsigned address, arguments[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  char extra;
  int count = sscanf(line, "call %x %x %x %x %x %x %x %x %x %c", &address, &arguments[0], &arguments[1], &arguments[2], &arguments[3],
                     &arguments[4], &arguments[5], &arguments[6], &arguments[7], &extra);
  if (count < 1 || count > 9) {
    reply("error call: usage: call <addr-hex> [r3 .. r10] (up to 8 hex arguments)");
    fprintf(stderr, "control: call refused: usage: call <addr-hex> [r3 .. r10] (up to 8 hex arguments)\n");
    return;
  }
  if (!translated_at(address)) {
    reply("error call: no translated function starts at 0x%08x", address);
    fprintf(stderr, "control: call 0x%08x refused: no translated function starts there\n", address);
    return;
  }
  request.kind = REQUEST_CALL, request.address = address;
  for (int i = 0; i < 8; i++) request.arguments[i] = arguments[i];
  if (!run_on_game_thread("call")) return;
  if (request.error[0]) reply("error call: %s", request.error);
  else reply("ok call %08x r3=%08x", address, request.result);
}

static void run_command_inner(const char *line) {
  char path[1024];
  int first, last;
  unsigned address;
  if (!strncmp(line, "reply on", 8)) { reply_enabled = 1; reply("ok reply on"); return; }
  if (!strncmp(line, "reply off", 9)) { reply("ok reply off"); reply_enabled = 0; return; }
  if (input_run_command(line) || pads_command(line)) return;
  if (!strncmp(line, "poke ", 5)) poke_command(line);
  else if (!strncmp(line, "pokef ", 6)) pokef_command(line);
  else if (!strncmp(line, "call ", 5)) call_command(line);
  else if (!strncmp(line, "reset", 5)) reset_button_press();
  else if (!strncmp(line, "power", 5)) { if (!power_button_press()) fprintf(stderr, "control: nothing is listening for the power button\n"); }
  else if (!strncmp(line, "dump", 4)) gx_inspect_dump();
  else if (!strncmp(line, "threads", 7)) debug_threads_now = 1;  // Listed on the game's thread, at its next check.
  else if (sscanf(line, "record-sound %d %1023s", &first, path) == 2) dsp_record_request(first, path);
  else if (sscanf(line, "record %d %1023s", &first, path) == 2) gx_record_request(first, path);
  else if (sscanf(line, "timeline %d %1023s", &first, path) == 2 && first > 0) timeline_request((unsigned)first, path);
  else if (sscanf(line, "skip %d %d", &first, &last) == 2) gx_inspect_skip(first, last);
  else if (sscanf(line, "shot %1023s", path) == 1) gx_shot(path);
  else if (sscanf(line, "peek %x %d", &address, &first) == 2) peek(address, first);
  else if (!strncmp(line, "state", 5)) { interrupts_describe(); hardware_describe(); }
  else if (!strncmp(line, "eject", 5)) console_request_disc(0);
  else if (sscanf(line, "insert %1023s", path) == 1) console_request_disc(path);
  else {
    fprintf(stderr, "control: unknown command: %s", line);
    reply("error unknown command");
  }
}

// Every command gets one reply line: its own if it made one, "ok" if it didn't.
static void run_command(const char *line) {
  reply_sent = 0;
  run_command_inner(line);
  if (!reply_sent) reply("ok");
}

static void *control_loop(void *path) {
  host_thread_name("ctl-pipe");
  // Opened for writing too, so it stays open between senders.
  FILE *pipe_file = fdopen(open(path, O_RDWR), "r");
  char line[1200];  // Room for a shot command with a long path.
  while (pipe_file && fgets(line, sizeof line, pipe_file)) run_command(line);
  return 0;
}

#ifdef _WIN32
#include <windows.h>
// Windows has no named pipes in the file system, so WII_RECOMP_CONTROL names a pipe
// there: "wii" is \\.\pipe\wii. One sender at a time; the pipe opens again after each.
static HANDLE reply_pipe;
static void pipe_write(const char *text) {
  char line[4100];
  int length = snprintf(line, sizeof line, "%s\n", text);
  DWORD written;
  if (reply_pipe && !WriteFile(reply_pipe, line, (DWORD)length, &written, 0)) fprintf(stderr, "control: a reply could not be sent (error %lu)\n", GetLastError());
}

static void *control_pipe_loop(void *name) {
  host_thread_name("ctl-pipe");
  char path[300], line[1200];
  const char *last = strrchr((const char *)name, '/');
  snprintf(path, sizeof path, "\\\\.\\pipe\\%s", last ? last + 1 : (const char *)name);
  while (1) {
    HANDLE pipe = CreateNamedPipeA(path, PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_WAIT, 1, 65536, 4096, 0, 0);
    if (pipe == INVALID_HANDLE_VALUE) return 0;
    if (ConnectNamedPipe(pipe, 0) || GetLastError() == ERROR_PIPE_CONNECTED) {
      reply_pipe = pipe;
      reply_enabled = 0;
      size_t length = 0;
      char byte;
      DWORD got;
      while (ReadFile(pipe, &byte, 1, &got, 0) && got == 1) {
        if (byte == '\r') continue;
        if (length < sizeof line - 2) line[length++] = byte;
        if (byte == '\n') { line[length] = 0; run_command(line); length = 0; }
      }
      if (length) { line[length++] = '\n'; line[length] = 0; run_command(line); }
    }
    reply_pipe = 0;
    reply_enabled = 0;
    CloseHandle(pipe);
  }
}

void input_start_control_pipe(const char *path) {
  static pthread_t thread;
  pthread_create(&thread, 0, control_pipe_loop, (void *)path);
}
#else
static void pipe_write(const char *text) { (void)text; }  // The FIFO is one-way: replies are on stderr only.

void input_start_control_pipe(const char *path) {
  static pthread_t thread;
  unlink(path);
  mkfifo(path, 0600);
  pthread_create(&thread, 0, control_loop, (void *)path);
}
#endif
