// The game's window. The Mac needs it created and serviced on the main thread;
// drawing happens on the game's thread.
#ifdef GX_WITH_VULKAN
#include <vulkan/vulkan.h>
#define GLFW_INCLUDE_VULKAN
#else
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>
#ifdef __APPLE__
#include <objc/message.h>
#include <objc/runtime.h>
#endif
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "console.h"
#include "controllers.h"
#include "gx_backend.h"
#include "hardware.h"
#include "pads.h"
#include "input.h"
#include "ios.h"
#include "window_input.h"

enum { WINDOW_HEIGHT = 720, POWER_OFF_SECONDS = 5 };

static GLFWwindow *window;
static int frame_width, frame_height;

// Full screen: F11 or Alt+Enter switch, WII_RECOMP_FULLSCREEN=1 starts in it. The whole
// monitor the window is on, at its own mode, so a TV or a game console box shows the game
// alone, above whatever menu started it; the pointer hides there, as the mouse is only a
// remote's pointer. The window comes back where and as big as it was.
static int fullscreen, windowed_x, windowed_y, windowed_width, windowed_height;

static GLFWmonitor *monitor_of_window(void) {
  int count = 0, x, y, width, height;
  GLFWmonitor **monitors = glfwGetMonitors(&count);
  glfwGetWindowPos(window, &x, &y);
  glfwGetWindowSize(window, &width, &height);
  int center_x = x + width / 2, center_y = y + height / 2;
  for (int i = 0; i < count; i++) {
    int mx, my;
    const GLFWvidmode *mode = glfwGetVideoMode(monitors[i]);
    glfwGetMonitorPos(monitors[i], &mx, &my);
    if (mode && center_x >= mx && center_x < mx + mode->width && center_y >= my && center_y < my + mode->height) return monitors[i];
  }
  return glfwGetPrimaryMonitor();
}

static void set_fullscreen(int on) {
  if (on == fullscreen) return;
  if (on) {
    glfwGetWindowPos(window, &windowed_x, &windowed_y);
    glfwGetWindowSize(window, &windowed_width, &windowed_height);
    GLFWmonitor *monitor = monitor_of_window();
    const GLFWvidmode *mode = monitor ? glfwGetVideoMode(monitor) : 0;
    if (!mode) return;
    glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
  } else {
    glfwSetWindowMonitor(window, 0, windowed_x, windowed_y, windowed_width, windowed_height, GLFW_DONT_CARE);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
  }
  fullscreen = on;
}

static void on_key(GLFWwindow *from, int key, int scancode, int action, int mods) {
  (void)from; (void)scancode;
  if (action == GLFW_REPEAT) return;
  if (action == GLFW_PRESS && (key == GLFW_KEY_F11 || (key == GLFW_KEY_ENTER && (mods & GLFW_MOD_ALT)))) {
    set_fullscreen(!fullscreen);
    return;
  }
  window_input_key(key, action == GLFW_PRESS);
}

static void on_mouse_move(GLFWwindow *from, double x, double y) {
  int width, height;
  glfwGetWindowSize(from, &width, &height);
  window_input_mouse_move(x, y, width, height);
}

static void on_mouse_button(GLFWwindow *from, int button, int action, int mods) {
  (void)from; (void)mods;
  window_input_mouse_button(button, action == GLFW_PRESS);
}

// A Mac slows down a program whose window is covered or out of sight, which stretches
// the game's waits from milliseconds to a tenth of a second and drops it to 10 fps.
// A game keeps its own time with sound playing, as a console does, so it says so.
static void stay_awake(void) {
#ifdef __APPLE__
  // NSActivityUserInitiatedAllowingIdleSystemSleep and NSActivityLatencyCritical.
  const unsigned long long ACTIVITY_USER_INITIATED = 0x00FFFFFFULL & ~(1ULL << 20), ACTIVITY_LATENCY_CRITICAL = 0xFF00000000ULL;
  id info = ((id (*)(id, SEL))objc_msgSend)((id)objc_getClass("NSProcessInfo"), sel_registerName("processInfo"));
  id reason = ((id (*)(id, SEL, const char *))objc_msgSend)((id)objc_getClass("NSString"), sel_registerName("stringWithUTF8String:"), "running a game");
  id activity = ((id (*)(id, SEL, unsigned long long, id))objc_msgSend)(info, sel_registerName("beginActivityWithOptions:reason:"),
                                                                       ACTIVITY_USER_INITIATED | ACTIVITY_LATENCY_CRITICAL, reason);
  ((id (*)(id, SEL))objc_msgSend)(activity, sel_registerName("retain"));  // Held for as long as the program runs.
#endif
}

// WII_RECOMP_WINDOW=1920x1440 picks how many pixels the window starts with, in either
// case of x. The game's own picture keeps its shape either way; this only says whether
// the upscale to the window grows the picture or shrinks it.
static void window_size_from_environment(int *width, int *height) {
  const char *wanted = getenv("WII_RECOMP_WINDOW");
  if (!wanted || !*wanted) return;
  char text[64];
  size_t length = strlen(wanted);
  if (length >= sizeof text) length = sizeof text - 1;
  memcpy(text, wanted, length);
  text[length] = '\0';
  for (char *p = text; *p; p++) if (*p == 'X') *p = 'x';
  int w, h;
  char extra;
  if (sscanf(text, "%dx%d%c", &w, &h, &extra) == 2 && w >= 160 && h >= 120 && w <= 16384 && h <= 16384) {
    *width = w;
    *height = h;
  } else {
    fprintf(stderr, "window: WII_RECOMP_WINDOW wants WIDTHxHEIGHT, as in 1920x1440; keeping %dx%d\n", *width, *height);
  }
}

// A window taller than the screen leaves its own controls off it, so a size that doesn't
// fit is brought down to the largest one of the game's shape that does.
static void fit_to_screen(int *width, int *height, int aspect_width, int aspect_height) {
  int x, y, area_width, area_height;
  GLFWmonitor *monitor = glfwGetPrimaryMonitor();
  if (!monitor) return;
  glfwGetMonitorWorkarea(monitor, &x, &y, &area_width, &area_height);
  if (area_width <= 0 || area_height <= 0) return;
  area_height -= 40;  // The title bar and frame.
  if (*width <= area_width && *height <= area_height) return;
  int fitted_height = area_height;
  int fitted_width = fitted_height * aspect_width / aspect_height;
  if (fitted_width > area_width) {
    fitted_width = area_width;
    fitted_height = fitted_width * aspect_height / aspect_width;
  }
  fprintf(stderr, "window: %dx%d won't fit the screen; using %dx%d\n", *width, *height, fitted_width, fitted_height);
  *width = fitted_width;
  *height = fitted_height;
}

void window_create(void) {
#ifdef GX_WITH_VULKAN
  // GLFW looks for the Vulkan loader on its own, and on the Mac it doesn't find
  // Homebrew's, so it's handed the one the console is linked with. GLFW 3.3, which
  // Ubuntu 24.04 still ships, has no such call and finds the system's loader itself.
#if GLFW_VERSION_MAJOR > 3 || (GLFW_VERSION_MAJOR == 3 && GLFW_VERSION_MINOR >= 4)
  glfwInitVulkanLoader(vkGetInstanceProcAddr);
#endif
#endif
  if (!glfwInit()) { fprintf(stderr, "stop: no window system\n"); exit(10); }
  stay_awake();
  gx_backend_choose();
  gx_backend->window_prepare();
  glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_FALSE);  // The game draws 640x456; 4x the pixels adds cost, not detail.
  // The console's own setting says whether the TV is widescreen (IPL.AR), and games draw
  // for it, so the window has that shape and keeps it when resized.
  uint8_t widescreen = 0;
  files_system_setting("IPL.AR", &widescreen, 1);
  int aspect_width = widescreen ? 16 : 4, aspect_height = widescreen ? 9 : 3;
  int width = WINDOW_HEIGHT * aspect_width / aspect_height, height = WINDOW_HEIGHT;
  window_size_from_environment(&width, &height);
  fit_to_screen(&width, &height, aspect_width, aspect_height);
  fprintf(stderr, "window: asking for %dx%d, aspect %d:%d\n", width, height, aspect_width, aspect_height);
  window = glfwCreateWindow(width, height, "Wii Recomp", 0, 0);
  if (!window) { fprintf(stderr, "stop: no window\n"); exit(10); }
  {
    int window_width = 0, window_height = 0, pixel_width = 0, pixel_height = 0;
    glfwGetWindowSize(window, &window_width, &window_height);
    glfwGetFramebufferSize(window, &pixel_width, &pixel_height);
    fprintf(stderr, "window: created %dx%d, framebuffer %dx%d\n", window_width, window_height, pixel_width, pixel_height);
  }
  glfwSetWindowAspectRatio(window, aspect_width, aspect_height);
  glfwSetKeyCallback(window, on_key);
  glfwSetCursorPosCallback(window, on_mouse_move);
  glfwSetMouseButtonCallback(window, on_mouse_button);
  gx_backend->window_ready(window);
  const char *wanted = getenv("WII_RECOMP_FULLSCREEN");
  if (wanted && wanted[0] && wanted[0] != '0') set_fullscreen(1);
}

struct GLFWwindow *window_handle(void) { return window; }

void window_refresh_size(void) { glfwGetFramebufferSize(window, &frame_width, &frame_height); }

void window_size(int *width, int *height) {
  if (!frame_width) window_refresh_size();
  *width = frame_width;
  *height = frame_height;
}

// Closing the window is pressing the power button: the program turns the console off
// its own way, writing what it needs to. One that doesn't listen, or takes too long,
// has its power cut, with open files written out as IOS would.
//
// SIGTERM and SIGINT press it too, so a computer that is only the console (such as a
// Raspberry Pi started by recomp/tools/pi_setup.sh) turns off as the Wii does when the
// system stops it. The window wakes every 50 ms at most to see the flag; a second signal
// is the system's default, which ends the console at once.
static volatile sig_atomic_t stop_signal;

static void on_stop_signal(int number) {
  stop_signal = number;
  signal(number, SIG_DFL);
}

void window_run(void) {
  signal(SIGTERM, on_stop_signal);
  signal(SIGINT, on_stop_signal);
  while (!glfwWindowShouldClose(window) && !stop_signal) {
    glfwWaitEventsTimeout(pads_wait_seconds());
    pads_poll();
  }
  if (stop_signal) fprintf(stderr, "console: asked to stop (signal %d)\n", (int)stop_signal);
  else fprintf(stderr, "console: the window was asked to close\n");  // By the window system: its close button, or a program closing it.
  if (power_button_press()) {
    fprintf(stderr, "console: the power button is pressed\n");
    for (double until = glfwGetTime() + POWER_OFF_SECONDS; glfwGetTime() < until;) {
      glfwWaitEventsTimeout(0.05);
      pads_poll();
    }
    fprintf(stderr, "console: the program didn't turn off within %d seconds\n", POWER_OFF_SECONDS);
  }
  console_power_off("off");
}
