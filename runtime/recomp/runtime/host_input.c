// What a program that hosts the console (an Android app, say) calls to be a player's controls: buttons, the
// Nunchuk's stick and the pointer. Plain C functions, safe from any thread; the simulated devices read the result on
// the game's thread, one consistent snapshot per report (input.c). They are the same controls the control pipe's
// press, stick and pointer verbs work, kept in their own share of the buttons so a touch lets go only of its own.
// android_jni.c is the Android app's way in.
#include <stdint.h>

#include "input.h"

// Sets every button this host holds, as a mask of BUTTON_* (input.h): A 0x0800, B 0x0400, 1 0x0200, 2 0x0100,
// - 0x1000, + 0x0010, Home 0x8000, D-pad left 1, right 2, down 4, up 8, Nunchuk C 0x100000 and Z 0x200000.
void wii_recomp_input_buttons(int player, uint32_t buttons) { input_host_buttons(player, buttons); }

// The Nunchuk's stick, -1 to 1 each way with y growing up.
void wii_recomp_input_stick(int player, float x, float y) {
  if (x < -1) x = -1;
  if (x > 1) x = 1;
  if (y < -1) y = -1;
  if (y > 1) y = 1;
  input_host_stick(player, x, y);
}

// The pointer, -1 to 1 across the game's picture, y growing down. Where the picture is in a window is
// wii_recomp_input_pointer_in_window's business (window_sdl.c).
void wii_recomp_input_pointer(int player, float x, float y) {
  if (x < -1) x = -1;
  if (x > 1) x = 1;
  if (y < -1) y = -1;
  if (y > 1) y = 1;
  input_pointer_from(INPUT_HOST, player, x, y);
}
