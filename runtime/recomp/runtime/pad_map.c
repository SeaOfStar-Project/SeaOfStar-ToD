// The mappings and the gyro pointer pad_map.h describes.
#include <math.h>
#include <string.h>

#include "pad_map.h"

static const struct { const char *name; uint32_t button; } names[] = {
  {"a", PAD_A}, {"b", PAD_B}, {"x", PAD_X}, {"y", PAD_Y}, {"l", PAD_L}, {"r", PAD_R}, {"zl", PAD_ZL}, {"zr", PAD_ZR},
  {"plus", PAD_PLUS}, {"minus", PAD_MINUS}, {"home", PAD_HOME}, {"capture", PAD_CAPTURE},
  {"left_stick", PAD_LEFT_STICK}, {"right_stick", PAD_RIGHT_STICK},
  {"dpad_up", PAD_UP}, {"dpad_down", PAD_DOWN}, {"dpad_left", PAD_LEFT}, {"dpad_right", PAD_RIGHT},
};

uint32_t pad_button_named(const char *name) {
  for (size_t i = 0; i < sizeof names / sizeof *names; i++) {
    if (!strcmp(name, names[i].name)) return names[i].button;
  }
  return 0;
}

PadLayout pad_layout(Attachment attachment, int sideways) {
  if (attachment == ATTACHMENT_CLASSIC) return PAD_AS_CLASSIC;
  return sideways ? PAD_AS_SIDEWAYS : PAD_AS_REMOTE;
}

typedef struct { uint32_t from, to; } Pair;

static uint32_t mapped(uint32_t buttons, const Pair *pairs, size_t count) {
  uint32_t out = 0;
  for (size_t i = 0; i < count; i++) {
    if (buttons & pairs[i].from) out |= pairs[i].to;
  }
  return out;
}

// Upright, as a remote pointed at the screen with a Nunchuk in the other hand: the
// right-hand side is the remote, with B under the finger on ZR, and the left-hand
// shoulders are the Nunchuk's C and Z. The Classic Controller's buttons sit where a
// Nintendo controller's do; its x and y are the remote's 1 and 2 in input.h. Sideways,
// the D-pad turns a quarter, so the way that's up in the hand is the remote's right.
static const Pair remote[] = {
  {PAD_A, BUTTON_A}, {PAD_B, BUTTON_B}, {PAD_ZR, BUTTON_B}, {PAD_Y, BUTTON_ONE}, {PAD_X, BUTTON_TWO},
  {PAD_PLUS, BUTTON_PLUS}, {PAD_MINUS, BUTTON_MINUS}, {PAD_HOME, BUTTON_HOME},
  {PAD_UP, BUTTON_UP}, {PAD_DOWN, BUTTON_DOWN}, {PAD_LEFT, BUTTON_LEFT}, {PAD_RIGHT, BUTTON_RIGHT},
  {PAD_L, BUTTON_NUNCHUK_C}, {PAD_ZL, BUTTON_NUNCHUK_Z},
};
static const Pair classic[] = {
  {PAD_A, BUTTON_A}, {PAD_B, BUTTON_B}, {PAD_X, BUTTON_ONE}, {PAD_Y, BUTTON_TWO},
  {PAD_L, BUTTON_CLASSIC_L}, {PAD_R, BUTTON_CLASSIC_R}, {PAD_ZL, BUTTON_CLASSIC_ZL}, {PAD_ZR, BUTTON_CLASSIC_ZR},
  {PAD_PLUS, BUTTON_PLUS}, {PAD_MINUS, BUTTON_MINUS}, {PAD_HOME, BUTTON_HOME},
  {PAD_UP, BUTTON_UP}, {PAD_DOWN, BUTTON_DOWN}, {PAD_LEFT, BUTTON_LEFT}, {PAD_RIGHT, BUTTON_RIGHT},
};
static const Pair sideways[] = {
  {PAD_B, BUTTON_ONE}, {PAD_A, BUTTON_TWO}, {PAD_X, BUTTON_A}, {PAD_Y, BUTTON_B},
  {PAD_PLUS, BUTTON_PLUS}, {PAD_MINUS, BUTTON_MINUS}, {PAD_HOME, BUTTON_HOME},
  {PAD_LEFT, BUTTON_UP}, {PAD_UP, BUTTON_RIGHT}, {PAD_RIGHT, BUTTON_DOWN}, {PAD_DOWN, BUTTON_LEFT},
};

// The Wii buttons a player can give another controller button, by name.
static const struct { const char *name; uint32_t button; } wii_names[] = {
  {"a", BUTTON_A}, {"b", BUTTON_B}, {"one", BUTTON_ONE}, {"two", BUTTON_TWO},
  {"plus", BUTTON_PLUS}, {"minus", BUTTON_MINUS}, {"home", BUTTON_HOME},
  {"up", BUTTON_UP}, {"down", BUTTON_DOWN}, {"left", BUTTON_LEFT}, {"right", BUTTON_RIGHT},
  {"c", BUTTON_NUNCHUK_C}, {"z", BUTTON_NUNCHUK_Z},
  {"classic_l", BUTTON_CLASSIC_L}, {"classic_r", BUTTON_CLASSIC_R},
  {"classic_zl", BUTTON_CLASSIC_ZL}, {"classic_zr", BUTTON_CLASSIC_ZR},
};
static struct { uint32_t button; int set; } bindings[sizeof wii_names / sizeof *wii_names];

int pad_map_bind(const char *wii, const char *button) {
  size_t index = 0;
  while (index < sizeof wii_names / sizeof *wii_names && strcmp(wii, wii_names[index].name)) index++;
  if (index == sizeof wii_names / sizeof *wii_names) return 0;
  if (!strcmp(button, "auto")) {
    bindings[index].set = 0;
    bindings[index].button = 0;
    return 1;
  }
  if (!strcmp(button, "none")) {
    bindings[index].set = 1;
    bindings[index].button = 0;
    return 1;
  }
  uint32_t named = pad_button_named(button);
  if (!named) return 0;
  bindings[index].set = 1;
  bindings[index].button = named;
  return 1;
}

void pad_map_defaults(void) { memset(bindings, 0, sizeof bindings); }

static uint32_t rebind(uint32_t mapped_buttons, uint32_t controller_buttons) {
  for (size_t i = 0; i < sizeof wii_names / sizeof *wii_names; i++) {
    if (!bindings[i].set) continue;
    mapped_buttons &= ~wii_names[i].button;
    if (bindings[i].button && (controller_buttons & bindings[i].button)) mapped_buttons |= wii_names[i].button;
  }
  return mapped_buttons;
}

// The sticks, by a circle in the middle where a worn one rests and a speed after it.
enum { STICK_DEADZONE_PERCENT = 18, STICK_SPEED_PERCENT = 100 };
static float stick_deadzone = STICK_DEADZONE_PERCENT / 100.0f;
static float stick_speed = STICK_SPEED_PERCENT / 100.0f;

void pad_stick_defaults(void) {
  stick_deadzone = STICK_DEADZONE_PERCENT / 100.0f;
  stick_speed = STICK_SPEED_PERCENT / 100.0f;
}

int pad_stick_set_deadzone(float deadzone) {
  if (!(deadzone >= 0 && deadzone <= 0.9f)) return 0;
  stick_deadzone = deadzone;
  return 1;
}

int pad_stick_set_speed(float speed) {
  if (!(speed >= 0.25f && speed <= 4)) return 0;
  stick_speed = speed;
  return 1;
}

void pad_stick_shape(const float raw[2], float out[2]) {
  float x = raw[0], y = raw[1];
  float size = sqrtf(x * x + y * y);
  if (size <= stick_deadzone) {
    out[0] = out[1] = 0;
    return;
  }
  // The rest of the travel is spread over the whole range, so a deadzone only removes
  // the centre and doesn't leave a jump at its edge.
  float turned = (size - stick_deadzone) / (1 - stick_deadzone) * stick_speed;
  if (turned > 1) turned = 1;
  out[0] = x / size * turned;
  out[1] = y / size * turned;
}

static const float STICK_AS_PAD = 0.5f;  // How far a stick goes to press a direction.

uint32_t pad_wii_buttons(const PadInput *pad, PadLayout layout) {
  uint32_t buttons = pad->buttons;
  if (layout == PAD_AS_SIDEWAYS) {  // One stick and few buttons: the stick works the D-pad too.
    if (pad->left[0] < -STICK_AS_PAD) buttons |= PAD_LEFT;
    if (pad->left[0] > STICK_AS_PAD) buttons |= PAD_RIGHT;
    if (pad->left[1] > STICK_AS_PAD) buttons |= PAD_UP;
    if (pad->left[1] < -STICK_AS_PAD) buttons |= PAD_DOWN;
    return rebind(mapped(buttons, sideways, sizeof sideways / sizeof *sideways), buttons);
  }
  if (layout == PAD_AS_CLASSIC) return rebind(mapped(buttons, classic, sizeof classic / sizeof *classic), buttons);
  return rebind(mapped(buttons, remote, sizeof remote / sizeof *remote), buttons);
}

void pad_remote_axes(const float pad[3], int sideways_held, float remote[3]) {
  if (sideways_held) {
    remote[0] = pad[2];
    remote[1] = pad[0];
  } else {
    remote[0] = pad[0];
    remote[1] = -pad[2];
  }
  remote[2] = pad[1];
}

enum { STILL_SECONDS = 1 };
static const float STILL_DEGREES = 3, STILL_G = 0.05f, GRAVITY_SECONDS = 0.08f, POINTER_LIMIT = 1.6f;
static const float VERTICAL_SHARE = 0.75f;  // The Wii's picture is 4:3: up and down is three quarters as far.

void pad_pointer_reset(PadPointer *pointer) {
  memset(pointer, 0, sizeof *pointer);
  pointer->gravity[1] = 1;
}

void pad_pointer_centre(PadPointer *pointer) { pointer->x = pointer->y = 0; }

static float dot(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static float clamp(float value, float limit) { return value < -limit ? -limit : value > limit ? limit : value; }

void pad_pointer_update(PadPointer *pointer, const float accel[3], const float gyro[3], float seconds, float speed, int sideways_held) {
  if (!(seconds > 0)) return;
  if (seconds > 0.1f) seconds = 0.1f;  // A gap in the samples isn't a turn.
  float size = sqrtf(dot(accel, accel));
  // Gravity, from samples that are mostly gravity, not a swing.
  if (size > 0.7f && size < 1.3f) {
    float weight = pointer->has_gravity ? seconds / GRAVITY_SECONDS : 1;
    if (weight > 1) weight = 1;
    for (int i = 0; i < 3; i++) pointer->gravity[i] += (accel[i] / size - pointer->gravity[i]) * weight;
    float length = sqrtf(dot(pointer->gravity, pointer->gravity));
    for (int i = 0; i < 3; i++) pointer->gravity[i] /= length;
    pointer->has_gravity = 1;
  }
  // A gyro reads a little even when still, and it drifts. While the controller is still
  // for a second, what it reads becomes the zero.
  float turn[3];
  int still = fabsf(size - 1) < STILL_G;
  for (int i = 0; i < 3; i++) {
    turn[i] = gyro[i] - pointer->bias[i];
    still = still && fabsf(turn[i]) < STILL_DEGREES;
  }
  if (still) {
    for (int i = 0; i < 3; i++) pointer->still_sum[i] += gyro[i];
    pointer->still_samples++;
    pointer->still_seconds += seconds;
    if (pointer->still_seconds >= STILL_SECONDS) {
      for (int i = 0; i < 3; i++) pointer->bias[i] = pointer->still_sum[i] / (float)pointer->still_samples;
      memset(pointer->still_sum, 0, sizeof pointer->still_sum);
      pointer->still_samples = 0;
      pointer->still_seconds = 0;
    }
  } else {
    memset(pointer->still_sum, 0, sizeof pointer->still_sum);
    pointer->still_samples = 0;
    pointer->still_seconds = 0;
  }
  // Turning left about the way up moves the pointer left; tipping the front up moves it up.
  static const float upright_across[3] = {1, 0, 0}, sideways_across[3] = {0, 0, 1};
  float yaw = dot(turn, pointer->gravity), pitch = dot(turn, sideways_held ? sideways_across : upright_across);
  pointer->x = clamp(pointer->x - yaw * seconds * speed / PAD_SCREEN_DEGREES, POINTER_LIMIT);
  pointer->y = clamp(pointer->y - pitch * seconds * speed / (PAD_SCREEN_DEGREES * VERTICAL_SHARE), POINTER_LIMIT);
}

float pad_roll(const PadPointer *pointer, int sideways_held) {
  float down[3];
  pad_remote_axes(pointer->gravity, sideways_held, down);
  return atan2f(-down[0], down[2]);
}

void pad_pointer_stick(PadPointer *pointer, const float stick[2], float seconds, float speed) {
  pointer->x = clamp(pointer->x + stick[0] * seconds * speed * 1.5f, 1);
  pointer->y = clamp(pointer->y - stick[1] * seconds * speed * 1.5f, 1);
}
