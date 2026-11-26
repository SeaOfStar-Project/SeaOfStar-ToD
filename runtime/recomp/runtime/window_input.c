// The keyboard and mouse as the players' Wii Remotes, whichever library the window comes
// from (window.c with GLFW, window_sdl.c with SDL3). Keys are GLFW's key codes, which is
// what controllers.ini names (controllers.c), and mouse buttons are GLFW's numbers
// (0 left, 1 right, 2 middle).
#include <stdint.h>
#include <stdlib.h>

#include "controllers.h"
#include "input.h"
#include "window_input.h"

// What each keyboard control presses on the keyboard's player's remote. C and Z work the
// Nunchuk's buttons, and by default the same keys work the Classic Controller's R and ZL;
// each attachment reads only its own.
static const uint32_t control_buttons[KEY_CONTROLS] = {
  [KEY_UP] = BUTTON_UP, [KEY_DOWN] = BUTTON_DOWN, [KEY_LEFT] = BUTTON_LEFT, [KEY_RIGHT] = BUTTON_RIGHT,
  [KEY_A] = BUTTON_A, [KEY_B] = BUTTON_B, [KEY_ONE] = BUTTON_ONE, [KEY_TWO] = BUTTON_TWO, [KEY_PLUS] = BUTTON_PLUS,
  [KEY_MINUS] = BUTTON_MINUS, [KEY_HOME] = BUTTON_HOME, [KEY_L] = BUTTON_CLASSIC_L, [KEY_R] = BUTTON_CLASSIC_R,
  [KEY_ZL] = BUTTON_CLASSIC_ZL, [KEY_ZR] = BUTTON_CLASSIC_ZR, [KEY_C] = BUTTON_NUNCHUK_C, [KEY_Z] = BUTTON_NUNCHUK_Z,
};

void window_input_key(int key, int down) {
  int player = controllers_keyboard_player();
  KeyControl controls[KEY_CONTROLS];
  int count = controllers_controls_for(key, controls, KEY_CONTROLS);
  // The stick keys tilt the Classic Controller's left stick, which games such as Mario
  // Kart steer with, and the Nunchuk's.
  static int stick[4];
  static float lean_x, lean_y;
  for (int i = 0; i < count; i++) {
    KeyControl control = controls[i];
    if (control_buttons[control] && player >= 0) input_hold_from(INPUT_KEYBOARD, player, control_buttons[control], down);
    if (control >= KEY_STICK_UP && control <= KEY_STICK_RIGHT && player >= 0) {
      stick[control - KEY_STICK_UP] = down;
      float x = (float)(stick[KEY_STICK_RIGHT - KEY_STICK_UP] - stick[KEY_STICK_LEFT - KEY_STICK_UP]);
      float y = (float)(stick[0] - stick[KEY_STICK_DOWN - KEY_STICK_UP]);
      input_stick(player, x, y);
      input_nunchuk_stick(player, x, y);
    }
    if (control == KEY_SHAKE && down && player >= 0) input_shake(player);
    // The next attachment: none, a Nunchuk, a Classic Controller.
    if (control == KEY_NEXT_ATTACHMENT && down && player >= 0) {
      input_attach(player, (Attachment)((input_attachment(player) + 1) % ATTACHMENT_KINDS));
    }
    if (control == KEY_MOTIONPLUS && down && player >= 0) input_attach_motionplus(player, !input_motionplus(player));
    if (control == KEY_BOARD && down) input_board_toggle();
    if (control == KEY_LEAN_LEFT) lean_x = down ? -0.6f : 0;
    if (control == KEY_LEAN_RIGHT) lean_x = down ? 0.6f : 0;
    if (control == KEY_LEAN_FORWARD) lean_y = down ? 0.6f : 0;
    if (control == KEY_LEAN_BACK) lean_y = down ? -0.6f : 0;
    if (control >= KEY_LEAN_LEFT && control <= KEY_LEAN_BACK) input_board_nudge(lean_x, lean_y);
  }
}


// A control script drives the remote alone, so a stray mouse can't move or click it.
static int scripted(void) { return getenv("WII_RECOMP_CONTROL") != 0; }

void window_input_mouse_move(double x, double y, int width, int height) {
  if (scripted()) return;
  int player = controllers_mouse_player();
  if (player >= 0 && controllers_player(player)->pointer & POINTER_MOUSE) {
    input_pointer_from(INPUT_MOUSE, player, (float)(x / width * 2 - 1), (float)(y / height * 2 - 1));
  }
}

void window_input_mouse_button(int button, int down) {
  if (scripted()) return;
  int player = controllers_mouse_player();
  if (player < 0) return;
  // [mouse] says what each button works: left is the remote's A and right its B unless changed.
  int control = controllers_mouse_control(button);
  if (control < 0) return;
  if (control_buttons[control]) input_hold_from(INPUT_MOUSE, player, control_buttons[control], down);
  if (control == KEY_SHAKE && down) input_shake(player);
}
