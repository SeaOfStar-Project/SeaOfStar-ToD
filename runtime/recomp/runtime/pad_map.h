// How a host controller stands in for a Wii Remote, with nothing of SDL in it, so every
// mapping and the gyro pointer can be tested with made-up samples. pads.c feeds it.
#pragma once
#include <stdint.h>
#include "input.h"

// A controller's buttons, by where they sit on a Nintendo controller: a on the right of
// the four, b at the bottom, x at the top, y on the left.
enum {
  PAD_A = 1 << 0, PAD_B = 1 << 1, PAD_X = 1 << 2, PAD_Y = 1 << 3, PAD_L = 1 << 4, PAD_R = 1 << 5, PAD_ZL = 1 << 6,
  PAD_ZR = 1 << 7, PAD_PLUS = 1 << 8, PAD_MINUS = 1 << 9, PAD_HOME = 1 << 10, PAD_CAPTURE = 1 << 11,
  PAD_LEFT_STICK = 1 << 12, PAD_RIGHT_STICK = 1 << 13, PAD_UP = 1 << 14, PAD_DOWN = 1 << 15, PAD_LEFT = 1 << 16,
  PAD_RIGHT = 1 << 17,
};

typedef enum { PAD_AS_REMOTE, PAD_AS_CLASSIC, PAD_AS_SIDEWAYS } PadLayout;

// A controller's buttons normally stand in for the remote's by the layout they are
// played in. pad_map_bind replaces one Wii button's source for every layout, so a game
// that wants B on another button can have it. The Wii button names are a, b, one, two,
// plus, minus, home, up, down, left, right, c and z (the Nunchuk), and classic_l,
// classic_r, classic_zl and classic_zr. The controller button names are those
// pad_button_named takes. "auto" gives the button back to the layout; "none" leaves it
// unused. Returns 0 for a name it doesn't know.
int pad_map_bind(const char *wii, const char *button);
// Every button back to the layout's own.
void pad_map_defaults(void);
// How far a stick must leave its centre before it counts, 0 to 0.9, and how far it turns
// after that, 0.25 to 4. pad_stick_shape applies both in one step.
void pad_stick_defaults(void);
int pad_stick_set_deadzone(float deadzone);
int pad_stick_set_speed(float speed);
void pad_stick_shape(const float raw[2], float out[2]);

// The controller now: its buttons, its sticks (-1 to 1, y growing up) and triggers (0 to 1).
typedef struct {
  uint32_t buttons;
  float left[2], right[2], triggers[2];
} PadInput;

uint32_t pad_button_named(const char *name);  // 0 for none or an unknown name.
PadLayout pad_layout(Attachment attachment, int sideways);
// The Wii Remote's buttons (and the attachment's) for a controller laid out this way.
uint32_t pad_wii_buttons(const PadInput *pad, PadLayout layout);

// A controller's motion, in SDL's axes (x right, y up, z towards its holder), into the
// remote's (input.h): x to its right, y towards the screen, z out of its face. Held
// sideways, the remote's length runs along the controller's x.
void pad_remote_axes(const float pad[3], int sideways, float remote[3]);

// The pointer from the gyro, and which way is down from the accelerometer.
typedef struct {
  float x, y;           // Where it points, -1 to 1 across the screen, y growing down.
  float gravity[3];     // Which way is up, in SDL's axes, smoothed: a unit vector.
  float bias[3];        // The gyro's reading when still, in degrees a second.
  float still_seconds;  // How long it has been still.
  float still_sum[3];   // Its readings while still, for the next bias.
  int still_samples;
  int has_gravity;
} PadPointer;

void pad_pointer_reset(PadPointer *pointer);
void pad_pointer_centre(PadPointer *pointer);
// One sample: accel in g and gyro in degrees a second, both in SDL's axes, `seconds`
// after the last. A turn of PAD_SCREEN_DEGREES / speed reaches the screen's side from its
// middle. Turning left and right is about whichever way is up, however the controller is
// held, as people expect from a pointer.
void pad_pointer_update(PadPointer *pointer, const float accel[3], const float gyro[3], float seconds, float speed, int sideways);
// The remote's roll from gravity: turned clockwise as its holder sees it, in radians.
float pad_roll(const PadPointer *pointer, int sideways);
// A stick moving the pointer, as a mouse would: full tilt crosses the screen in a second or so.
void pad_pointer_stick(PadPointer *pointer, const float stick[2], float seconds, float speed);

enum { PAD_SCREEN_DEGREES = 20 };
