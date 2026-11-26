// The game's window on SDL3, which is how Android (and any platform without GLFW) gets one:
// the window, its Vulkan surface, the keyboard, the mouse (touch arrives as a mouse, which
// SDL3 does on its own) and the app's life cycle. window.c is the same for GLFW. Both give
// the console what gx.h declares, and share what keys and the mouse do (window_input.c).
//
// SDL has one event queue, and the controllers (pads.c) drain it, so what isn't a
// controller's is handed to window_event through pads_other_event.
#ifdef __ANDROID__
#define _GNU_SOURCE  // sched_getaffinity, CPU_COUNT
#endif
#include <SDL3/SDL.h>
#include <signal.h>
#ifdef __ANDROID__
#include <sched.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "console.h"
#include "controllers.h"
#include "gx_backend.h"
#include "hardware.h"
#include "ios.h"
#include "pads.h"
#include "window_input.h"

enum { WINDOW_HEIGHT = 720, POWER_OFF_SECONDS = 5 };

static SDL_Window *window;
static int frame_width, frame_height;
static volatile int close_asked;

// SDL's scancodes (the key's place on the keyboard, as GLFW's keys are) to GLFW's key codes,
// which controllers.ini names. 0 for a key nothing can be bound to.
static int glfw_key(SDL_Scancode code) {
  if (code >= SDL_SCANCODE_A && code <= SDL_SCANCODE_Z) return 65 + (code - SDL_SCANCODE_A);
  if (code >= SDL_SCANCODE_1 && code <= SDL_SCANCODE_9) return 49 + (code - SDL_SCANCODE_1);
  if (code == SDL_SCANCODE_0) return 48;
  if (code >= SDL_SCANCODE_F1 && code <= SDL_SCANCODE_F12) return 290 + (code - SDL_SCANCODE_F1);
  switch (code) {
    case SDL_SCANCODE_SPACE: return 32;
    case SDL_SCANCODE_APOSTROPHE: return 39;
    case SDL_SCANCODE_COMMA: return 44;
    case SDL_SCANCODE_MINUS: return 45;
    case SDL_SCANCODE_PERIOD: return 46;
    case SDL_SCANCODE_SLASH: return 47;
    case SDL_SCANCODE_SEMICOLON: return 59;
    case SDL_SCANCODE_EQUALS: return 61;
    case SDL_SCANCODE_LEFTBRACKET: return 91;
    case SDL_SCANCODE_BACKSLASH: return 92;
    case SDL_SCANCODE_RIGHTBRACKET: return 93;
    case SDL_SCANCODE_GRAVE: return 96;
    case SDL_SCANCODE_ESCAPE: return 256;
    case SDL_SCANCODE_RETURN: return 257;
    case SDL_SCANCODE_TAB: return 258;
    case SDL_SCANCODE_BACKSPACE: return 259;
    case SDL_SCANCODE_INSERT: return 260;
    case SDL_SCANCODE_DELETE: return 261;
    case SDL_SCANCODE_RIGHT: return 262;
    case SDL_SCANCODE_LEFT: return 263;
    case SDL_SCANCODE_DOWN: return 264;
    case SDL_SCANCODE_UP: return 265;
    case SDL_SCANCODE_PAGEUP: return 266;
    case SDL_SCANCODE_PAGEDOWN: return 267;
    case SDL_SCANCODE_HOME: return 268;
    case SDL_SCANCODE_END: return 269;
    case SDL_SCANCODE_LSHIFT: return 340;
    case SDL_SCANCODE_LCTRL: return 341;
    case SDL_SCANCODE_LALT: return 342;
    case SDL_SCANCODE_RSHIFT: return 344;
    case SDL_SCANCODE_RCTRL: return 345;
    case SDL_SCANCODE_RALT: return 346;
    default: return 0;
  }
}

// GLFW numbers the mouse buttons left, right, middle; SDL left, middle, right.
static int glfw_button(int button) { return button == SDL_BUTTON_LEFT ? 0 : button == SDL_BUTTON_RIGHT ? 1 : button == SDL_BUTTON_MIDDLE ? 2 : 3 + button; }

// The largest 4:3 (16:9 where the console's setting says the TV is widescreen) rectangle in the middle of a
// window of this size: where the game's picture goes (gx_vulkan_device.c, vk_window_rect) and so where a touch
// means a point of it. A phone's screen is wider than either.
// Whether the console says the TV is widescreen, as the game's thread last read it, for other threads
// (wii_recomp_input_pointer_in_window): the setting itself is the game's thread's to read.
static volatile int widescreen_seen;

static void letterbox_of(int width, int height, int widescreen, int *x, int *y, int *w, int *h) {
  int across = widescreen ? 16 : 4, down = widescreen ? 9 : 3;
  *x = *y = 0;
  *w = width;
  *h = height;
  if (width * down > height * across) {
    *w = height * across / down;
    *x = (width - *w) / 2;
  } else {
    *h = width * down / across;
    *y = (height - *h) / 2;
  }
}

void window_letterbox(int width, int height, int *x, int *y, int *w, int *h) {
  static uint8_t widescreen;
  static unsigned asked;
  if (!(asked++ % 60)) files_system_setting("IPL.AR", &widescreen, 1);
  widescreen_seen = widescreen;
  letterbox_of(width, height, widescreen, x, y, w, h);
}

// A host's touch at a point of the window (in pixels, as wide and high as the window's surface), as the pointer: the
// same mapping the mouse has, with a touch outside the picture at its nearest edge. Safe from any thread. With
// WII_RECOMP_STRETCH the picture fills the window and so does the mapping.
void wii_recomp_input_pointer_in_window(int player, float at_x, float at_y, int width, int height) {
  extern void wii_recomp_input_pointer(int player, float x, float y);
  if (width <= 0 || height <= 0) return;
  static int stretch = -1;
  if (stretch < 0) stretch = getenv("WII_RECOMP_STRETCH") != 0;
  int x = 0, y = 0, w = width, h = height;
  if (!stretch) letterbox_of(width, height, widescreen_seen, &x, &y, &w, &h);
  float u = (at_x - (float)x) / (float)w, v = (at_y - (float)y) / (float)h;
  if (u < 0) u = 0;
  if (u > 1) u = 1;
  if (v < 0) v = 0;
  if (v > 1) v = 1;
  wii_recomp_input_pointer(player, u * 2 - 1, v * 2 - 1);
}

// The picture's rectangle in a window of this size, for a host that draws something over it. 1 when the console's
// setting is widescreen. Safe from any thread.
int wii_recomp_picture_rect(int width, int height, int *x, int *y, int *w, int *h) {
  letterbox_of(width, height, widescreen_seen, x, y, w, h);
  return widescreen_seen;
}

static void window_event(const void *given) {
  const SDL_Event *event = given;
  switch (event->type) {
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED: close_asked = 1; break;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
      int key = glfw_key(event->key.scancode);
      if (key && !event->key.repeat) window_input_key(key, event->key.down);
      break;
    }
    case SDL_EVENT_MOUSE_MOTION: {
      int width = 0, height = 0;
      SDL_GetWindowSize(window, &width, &height);
      if (width > 0 && height > 0) {
        // The picture sits in a letterbox where the window's shape isn't the game's; a touch outside it is its nearest edge.
        int x, y, w, h;
        window_letterbox(width, height, &x, &y, &w, &h);
        double at_x = event->motion.x - x, at_y = event->motion.y - y;
        if (at_x < 0) at_x = 0;
        if (at_y < 0) at_y = 0;
        if (at_x > w) at_x = w;
        if (at_y > h) at_y = h;
        window_input_mouse_move(at_x, at_y, w, h);
      }
      break;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: window_input_mouse_button(glfw_button(event->button.button), event->button.down); break;
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: window_refresh_size(); break;
    // The app's life cycle. The window's surface may be lost in the background; the graphics side makes a
    // new one when presenting says so (vk_recreate_surface). Nothing saves the game here.
    case SDL_EVENT_WILL_ENTER_BACKGROUND: fprintf(stderr, "window: the app is going to the background (its surface may be lost, which the graphics side rebuilds)\n"); break;
    case SDL_EVENT_DID_ENTER_FOREGROUND: fprintf(stderr, "window: the app is back in the foreground\n"); break;
    case SDL_EVENT_TERMINATING: fprintf(stderr, "window: the system is ending the app\n"); close_asked = 1; break;
    case SDL_EVENT_LOW_MEMORY: fprintf(stderr, "window: the system is short of memory\n"); break;
    default: break;
  }
}

// WII_RECOMP_WINDOW=1920x1440 picks the window's size where a window can have any (not on a phone).
static void window_size_from_environment(int *width, int *height) {
  const char *wanted = getenv("WII_RECOMP_WINDOW");
  int w, h;
  char extra;
  if (!wanted || !*wanted) return;
  if (sscanf(wanted, "%dx%d%c", &w, &h, &extra) == 2 && w >= 160 && h >= 120 && w <= 16384 && h <= 16384) {
    *width = w;
    *height = h;
  } else {
    fprintf(stderr, "window: WII_RECOMP_WINDOW wants WIDTHxHEIGHT, as in 1920x1440; keeping %dx%d\n", *width, *height);
  }
}

// SDL_CreateWindow on Android waits (Android_WaitActiveAndLockActivity) until the Activity is resumed and has a
// surface, which it hasn't while the phone is locked, the screen is off or the app isn't in front (the recent apps
// screen, say). Nothing else of the console runs until then, so a start that seems stuck here says so, and the
// app's life cycle is logged while it waits.
static volatile int creating_window;
static Uint32 creating_since;

static Uint32 SDLCALL window_wait_note(void *unused, SDL_TimerID timer, Uint32 interval) {
  (void)unused, (void)timer;
  if (!creating_window) return 0;
  fprintf(stderr, "window: SDL_CreateWindow has waited %u s; on a phone it waits for the app to be in front with the screen on (not locked, not in the recent apps)\n",
          (unsigned)((SDL_GetTicks() - creating_since) / 1000));
  return interval;
}

static bool SDLCALL window_life_cycle_note(void *unused, SDL_Event *event) {
  (void)unused;
  switch (event->type) {
    case SDL_EVENT_WILL_ENTER_BACKGROUND: fprintf(stderr, "window: (while creating) will enter the background\n"); break;
    case SDL_EVENT_DID_ENTER_BACKGROUND: fprintf(stderr, "window: (while creating) entered the background\n"); break;
    case SDL_EVENT_WILL_ENTER_FOREGROUND: fprintf(stderr, "window: (while creating) will enter the foreground\n"); break;
    case SDL_EVENT_DID_ENTER_FOREGROUND: fprintf(stderr, "window: (while creating) entered the foreground\n"); break;
    case SDL_EVENT_WINDOW_FOCUS_GAINED: fprintf(stderr, "window: (while creating) focus gained\n"); break;
    case SDL_EVENT_WINDOW_FOCUS_LOST: fprintf(stderr, "window: (while creating) focus lost\n"); break;
    default: break;
  }
  return true;
}

void window_create(void) {
  fprintf(stderr, "window: starting SDL (video and events)\n");
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) { fprintf(stderr, "stop: no window system: %s\n", SDL_GetError()); exit(10); }
  fprintf(stderr, "window: SDL %d.%d.%d is up, video driver %s\n", SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_MICRO_VERSION, SDL_GetCurrentVideoDriver());
  gx_backend_choose();
  fprintf(stderr, "window: the graphics backend is chosen; preparing it for a window\n");
  gx_backend->window_prepare();
  pads_other_event = window_event;
  // With WII_RECOMP_NO_WINDOW nothing is shown: the picture goes where the backend's sink says
  // and there is no surface (the backend is told there is no window).
  const char *none = getenv("WII_RECOMP_NO_WINDOW");
  if (none && none[0] && none[0] != '0') {
    fprintf(stderr, "window: none asked for, so nothing is created or shown\n");
    gx_backend->window_ready(0);
    return;
  }
  SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
  uint8_t widescreen = 0;
  files_system_setting("IPL.AR", &widescreen, 1);
  int width = WINDOW_HEIGHT * (widescreen ? 16 : 4) / (widescreen ? 9 : 3), height = WINDOW_HEIGHT;
  window_size_from_environment(&width, &height);
  SDL_WindowFlags flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
#ifdef __ANDROID__
  flags |= SDL_WINDOW_FULLSCREEN;  // An app's window is the whole screen.
#endif
  fprintf(stderr, "window: asking for %dx%d%s\n", width, height, flags & SDL_WINDOW_FULLSCREEN ? " (the whole screen)" : "");
  creating_since = SDL_GetTicks();
  creating_window = 1;
  SDL_AddEventWatch(window_life_cycle_note, 0);
  SDL_TimerID waiting = SDL_AddTimer(5000, window_wait_note, 0);
  fprintf(stderr, "window: calling SDL_CreateWindow\n");
  window = SDL_CreateWindow("Wii Recomp", width, height, flags);
  creating_window = 0;
  if (waiting) SDL_RemoveTimer(waiting);
  SDL_RemoveEventWatch(window_life_cycle_note, 0);
  if (!window) { fprintf(stderr, "stop: no window: %s\n", SDL_GetError()); exit(10); }
  fprintf(stderr, "window: SDL_CreateWindow returned after %u ms\n", (unsigned)(SDL_GetTicks() - creating_since));
  {
    int window_width = 0, window_height = 0, pixel_width = 0, pixel_height = 0;
    SDL_GetWindowSize(window, &window_width, &window_height);
    SDL_GetWindowSizeInPixels(window, &pixel_width, &pixel_height);
    fprintf(stderr, "window: created %dx%d, framebuffer %dx%d\n", window_width, window_height, pixel_width, pixel_height);
  }
  fprintf(stderr, "window: handing the window to the graphics backend (its instance and surface)\n");
  gx_backend->window_ready((struct GLFWwindow *)window);  // The backend only passes it back to SDL.
  fprintf(stderr, "window: the graphics backend has the window\n");
}

struct GLFWwindow *window_handle(void) { return (struct GLFWwindow *)window; }

void window_refresh_size(void) {
  if (window) SDL_GetWindowSizeInPixels(window, &frame_width, &frame_height);
}

void window_size(int *width, int *height) {
  if (!frame_width) window_refresh_size();
  *width = frame_width;
  *height = frame_height;
}

// As window.c: closing is the power button, and a stop signal presses it too.
static volatile sig_atomic_t stop_signal;

static void on_stop_signal(int number) {
  stop_signal = number;
  signal(number, SIG_DFL);
}

// Waits for an event, up to a time, and takes whatever the controllers left.
static void wait_and_poll(double seconds) {
#ifdef __ANDROID__
  // SDL 3.4.20's SDL_WaitEventTimeoutNS on Android (SDL_events.c:1735) pumps the whole event system (joystick
  // detection, HIDAPI, sensors) on every pass and then waits only on the life-cycle semaphore; with a NULL event
  // (as here) it also returns at once while any event is queued. That cost the UI thread about 70% of a core.
  // Sleep instead, then pump once: pads_poll and the drain below are the pumps.
  SDL_DelayNS((Uint64)(seconds * 1e9));
#else
  SDL_WaitEventTimeout(0, (int32_t)(seconds * 1000));  // 0: the event stays queued for pads_poll.
#endif
  pads_poll();
  SDL_Event event;
  while (SDL_PollEvent(&event)) window_event(&event);  // What pads_poll didn't, which is all of it without controllers.
}

// How long the loop sleeps between pumps. Touch arrives through the app's own JNI path, so without a
// controller only life-cycle events and a newly plugged pad need this thread, and 16 ms is plenty.
static double window_wait_seconds(void) {
#ifdef __ANDROID__
  return pads_wait_seconds() < 0.01 ? pads_wait_seconds() : 0.016;
#else
  return pads_wait_seconds();
#endif
}

void window_run(void) {
#ifdef __ANDROID__
  {
    cpu_set_t mask;
    CPU_ZERO(&mask);
    int allowed = sched_getaffinity(0, sizeof mask, &mask) ? -1 : CPU_COUNT(&mask);
    fprintf(stderr, "window: the UI loop sleeps %.0f ms between pumps with a controller, %.0f ms without; it is left to the scheduler (%d cores allowed)\n",
            pads_wait_seconds() < 0.01 ? pads_wait_seconds() * 1000 : 4.0, 16.0, allowed);
  }
#endif
  signal(SIGTERM, on_stop_signal);
  signal(SIGINT, on_stop_signal);
  while (!close_asked && !stop_signal) wait_and_poll(window_wait_seconds());
  if (stop_signal) fprintf(stderr, "console: asked to stop (signal %d)\n", (int)stop_signal);
  else fprintf(stderr, "console: the window was asked to close\n");
  if (power_button_press()) {
    fprintf(stderr, "console: the power button is pressed\n");
    for (uint64_t until = SDL_GetTicks() + POWER_OFF_SECONDS * 1000; SDL_GetTicks() < until;) wait_and_poll(0.05);
    fprintf(stderr, "console: the program didn't turn off within %d seconds\n", POWER_OFF_SECONDS);
  }
  console_power_off("off");
}
