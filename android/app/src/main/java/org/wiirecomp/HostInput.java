package org.wiirecomp;

import android.util.Log;

/**
 * A host app's way to be a Wii Remote and Nunchuk: the native methods of libwiirecomp.so (android_jni.c), the
 * same controls the runtime's control pipe works. Safe to call from the UI thread at any time; before the runtime
 * is loaded, or if the library lacks the functions, the calls do nothing (isAvailable() says so after a try).
 */
public final class HostInput {
    // The Wii Remote's button bits, as the runtime's input.h has them.
    public static final int LEFT = 0x0001, RIGHT = 0x0002, DOWN = 0x0004, UP = 0x0008, PLUS = 0x0010;
    public static final int TWO = 0x0100, ONE = 0x0200, B = 0x0400, A = 0x0800, MINUS = 0x1000, HOME = 0x8000;
    public static final int NUNCHUK_C = 0x100000, NUNCHUK_Z = 0x200000;

    private static final String TAG = "SeaOfStar";
    private static volatile boolean available = true;

    private HostInput() {}

    private static native void nativeButtons(int player, int buttons);
    private static native void nativeStick(int player, float x, float y);
    private static native void nativePointer(int player, float x, float y, int width, int height);
    private static native int nativePictureRect(int width, int height, int[] out);

    public static boolean isAvailable() { return available; }

    private static void unavailable(UnsatisfiedLinkError e) {
        if (available) Log.w(TAG, "host input is not in the runtime library: " + e);
        available = false;
    }

    /** Every button held now, as a mask of the constants above (a whole set, so a missed release cannot stick). */
    public static void setButtons(int player, int mask) {
        if (!available) return;
        try { nativeButtons(player, mask); } catch (UnsatisfiedLinkError e) { unavailable(e); }
    }

    /** The Nunchuk's stick, -1 to 1 each way, y growing up. */
    public static void setStick(int player, float x, float y) {
        if (!available) return;
        try { nativeStick(player, x, y); } catch (UnsatisfiedLinkError e) { unavailable(e); }
    }

    /** The pointer at a point of a window width x height pixels; the runtime puts it on the game's picture (letterbox). */
    public static void setPointer(int player, float x, float y, int width, int height) {
        if (!available) return;
        try { nativePointer(player, x, y, width, height); } catch (UnsatisfiedLinkError e) { unavailable(e); }
    }

    /** The game's picture in a window of this size: out = x, y, width, height. Returns true when it is 16:9. */
    public static boolean pictureRect(int width, int height, int[] out) {
        if (!available) return false;
        try { return nativePictureRect(width, height, out) != 0; } catch (UnsatisfiedLinkError e) { unavailable(e); return false; }
    }
}
