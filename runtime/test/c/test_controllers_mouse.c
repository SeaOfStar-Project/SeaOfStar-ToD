// The [mouse] section of controllers.ini (controllers.c): its defaults, what it binds,
// what the file the console writes says about it, and the mistakes it catches by line.
// Compiled with controllers.c, settings_file.c, pad_map.c and input.c; needs no folder.
#include <GLFW/glfw3.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "controllers.h"

// No guest memory here, so the harness's CHECKs are small versions of their own.
static int failures;
#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); failures++; } } while (0)
#define CHECK_EQ(a, b) CHECK((a) == (b))
#define finish() (failures ? 1 : (puts("ok"), 0))

uint64_t time_base(void) { return 0; }

static int line_of(const char *text) {
  char error[256];
  return controllers_load_text(text, error, sizeof error);
}

int main(void) {
  // Left is the remote's A and right its B, as before the section existed; the rest is unused.
  CHECK_EQ(controllers_mouse_control(GLFW_MOUSE_BUTTON_LEFT), KEY_A);
  CHECK_EQ(controllers_mouse_control(GLFW_MOUSE_BUTTON_RIGHT), KEY_B);
  CHECK_EQ(controllers_mouse_control(GLFW_MOUSE_BUTTON_MIDDLE), -1);
  CHECK_EQ(controllers_mouse_control(GLFW_MOUSE_BUTTON_4), -1);
  CHECK_EQ(controllers_mouse_control(GLFW_MOUSE_BUTTON_5), -1);
  CHECK_EQ(controllers_mouse_control(7), -1);

  // The written file documents [mouse], and reads back as the defaults.
  const char *text = controllers_default_text();
  CHECK(strstr(text, "[mouse]") != 0);
  CHECK(strstr(text, "# x1 = none") != 0);
  CHECK_EQ(line_of(text), 0);

  CHECK_EQ(line_of("[mouse]\nleft = c\nright = shake\nmiddle = z\nx1 = zl\nx2 = none\n"), 0);
  CHECK_EQ(controllers_mouse_control(GLFW_MOUSE_BUTTON_LEFT), KEY_C);
  CHECK_EQ(controllers_mouse_control(GLFW_MOUSE_BUTTON_RIGHT), KEY_SHAKE);
  CHECK_EQ(controllers_mouse_control(GLFW_MOUSE_BUTTON_MIDDLE), KEY_Z);
  CHECK_EQ(controllers_mouse_control(GLFW_MOUSE_BUTTON_4), KEY_ZL);
  CHECK_EQ(controllers_mouse_control(GLFW_MOUSE_BUTTON_5), -1);
  CHECK_EQ(line_of("[mouse]\nleft = none\n"), 0);
  CHECK_EQ(controllers_mouse_control(GLFW_MOUSE_BUTTON_LEFT), -1);
  CHECK_EQ(controllers_mouse_control(GLFW_MOUSE_BUTTON_RIGHT), KEY_B);  // Unset ones stay at their defaults.

  // Each mistake is caught on its own line.
  CHECK_EQ(line_of("[mouse]\nwheel = a\n"), 2);
  CHECK_EQ(line_of("[mouse]\nleft = jump\n"), 2);
  CHECK_EQ(line_of("[mouse]\nleft = stick_up\n"), 2);
  CHECK_EQ(line_of("[mouse]\nmiddle = next_attachment\n"), 2);
  CHECK_EQ(line_of("[mouse]\nleft = a\nright = board\n"), 3);
  return finish();
}
