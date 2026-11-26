// The Android app's way into the host controls (host_input.c): the native methods of org.wiirecomp.HostInput,
// which the app's Java calls from its UI thread. Nothing here is the game's; any app that includes that class works.
#ifdef __ANDROID__
#include <jni.h>
#include <stdint.h>

void wii_recomp_input_buttons(int player, uint32_t buttons);
void wii_recomp_input_stick(int player, float x, float y);
void wii_recomp_input_pointer(int player, float x, float y);
void wii_recomp_input_pointer_in_window(int player, float at_x, float at_y, int width, int height);
int wii_recomp_picture_rect(int width, int height, int *x, int *y, int *w, int *h);

JNIEXPORT void JNICALL Java_org_wiirecomp_HostInput_nativeButtons(JNIEnv *env, jclass type, jint player, jint buttons) {
  (void)env, (void)type;
  wii_recomp_input_buttons(player, (uint32_t)buttons);
}

JNIEXPORT void JNICALL Java_org_wiirecomp_HostInput_nativeStick(JNIEnv *env, jclass type, jint player, jfloat x, jfloat y) {
  (void)env, (void)type;
  wii_recomp_input_stick(player, x, y);
}

JNIEXPORT void JNICALL Java_org_wiirecomp_HostInput_nativePointer(JNIEnv *env, jclass type, jint player, jfloat x, jfloat y, jint width, jint height) {
  (void)env, (void)type;
  wii_recomp_input_pointer_in_window(player, x, y, width, height);
}

// out gets x, y, width, height of the game's picture in a window of this size; returns 1 for widescreen, else 0.
JNIEXPORT jint JNICALL Java_org_wiirecomp_HostInput_nativePictureRect(JNIEnv *env, jclass type, jint width, jint height, jintArray out) {
  (void)type;
  int x, y, w, h;
  int wide = wii_recomp_picture_rect(width, height, &x, &y, &w, &h);
  jint values[4] = {x, y, w, h};
  if (out && (*env)->GetArrayLength(env, out) >= 4) (*env)->SetIntArrayRegion(env, out, 0, 4, values);
  return wide;
}
#endif
