package io.github.seaofstarproject.tod;

import android.content.Context;
import android.content.SharedPreferences;
import android.hardware.input.InputManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Process;
import android.system.ErrnoException;
import android.system.Os;
import android.util.Log;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.ViewGroup;
import android.widget.Toast;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;

import org.libsdl.app.SDLActivity;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;
import java.util.Map;

/**
 * The game. SDL's Activity loads libSDL3.so and libwiirecomp.so and runs the runtime's own
 * entry point (wiirecomp_main(argc, argv), the runtime's main() renamed) on its SDL thread.
 * The title library is not loaded here: the runtime takes it out of the image into
 * WII_RECOMP_CACHE and dlopens it (or uses WII_RECOMP_NATIVE for an extracted tree).
 * Runs in the :game process; leaving the game ends the process.
 */
public class GameActivity extends SDLActivity {
    private static final String TAG = "SeaOfStar";
    private String[] launchArguments = new String[0];
    private GamePaths gamePaths;
    private TouchOverlay overlay;
    private InputManager inputManager;
    private SharedPreferences preferences;
    // Back: a short press is the remote's + (the game's menu); holding it, then pressing it again within a few
    // seconds, leaves. Nothing asks in a dialog, which would take the window's focus from the game.
    private static final long BACK_HOLD_MS = 800, QUIT_WINDOW_MS = 3000;
    private long backDownAt, quitArmedUntil;
    private boolean backHoldHandled;

    private final InputManager.InputDeviceListener deviceListener = new InputManager.InputDeviceListener() {
        @Override public void onInputDeviceAdded(int deviceId) { updateControllerPresence("added " + deviceId); }
        @Override public void onInputDeviceRemoved(int deviceId) { updateControllerPresence("removed " + deviceId); }
        @Override public void onInputDeviceChanged(int deviceId) { updateControllerPresence("changed " + deviceId); }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        GamePaths paths = new GamePaths(this);
        gamePaths = paths;
        paths.ensureDirs();
        paths.rotateLog();
        Bundle extras = getIntent() == null ? null : getIntent().getExtras();
        String target = paths.bootTarget(extras);
        launchArguments = target == null ? new String[0] : new String[] {target};
        Map<String, String> env = paths.environment(extras, target);
        StringBuilder note = new StringBuilder("java: launching ").append(target == null ? "(nothing to boot)" : target);
        for (Map.Entry<String, String> entry : env.entrySet()) {
            try {
                Os.setenv(entry.getKey(), entry.getValue(), true);
                note.append("\njava: ").append(entry.getKey()).append('=').append(entry.getValue());
            } catch (ErrnoException e) {
                note.append("\njava: can't set ").append(entry.getKey()).append(": ").append(e);
            }
        }
        Log.i(TAG, note.toString());
        paths.logLine(note.toString());
        if (target == null) paths.logLine("java: no image or extracted tree in " + paths.dataDir);

        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        // Not Window.setSustainedPerformanceMode: it holds clocks at a level the phone can keep for hours, which for a
        // game that is CPU-bound on two big cores means lower clocks than the burst ones. The runtime asks for what it
        // needs per frame instead (APerformanceHint, host_perf.c), and android:appCategory="game" lets the system apply Game Mode.
        WindowManager.LayoutParams attributes = getWindow().getAttributes();
        attributes.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS;
        getWindow().setAttributes(attributes);
        super.onCreate(savedInstanceState);
        addTouchOverlay();
    }

    // ---- touch controls ----

    private String readLayout() {
        File mine = new File(gamePaths.dataDir, "touch_layout.json");
        if (mine.isFile()) {
            try (InputStream in = new FileInputStream(mine)) {
                String text = slurp(in);
                new org.json.JSONObject(text);   // a broken copy must not leave the game without controls
                return text;
            } catch (Exception e) {
                gamePaths.logLine("java: can't read " + mine + ", using the built-in touch layout: " + e);
            }
        }
        try (InputStream in = getAssets().open("touch_layout.json")) {
            return slurp(in);
        } catch (Exception e) {
            gamePaths.logLine("java: no touch layout: " + e);
            return null;
        }
    }

    private static String slurp(InputStream in) throws java.io.IOException {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        byte[] buffer = new byte[4096];
        for (int n; (n = in.read(buffer)) > 0; ) out.write(buffer, 0, n);
        return out.toString("UTF-8");
    }

    private void addTouchOverlay() {
        android.view.View content = SDLActivity.getContentView();
        ViewGroup layout = content instanceof ViewGroup ? (ViewGroup) content : null;
        String json = readLayout();
        if (layout == null || json == null) {
            gamePaths.logLine("java: no touch controls (" + (layout == null ? "no SDL layout" : "no layout file") + ")");
            return;
        }
        preferences = getSharedPreferences("touch", Context.MODE_PRIVATE);
        // The launcher's settings (extras) decide the first state; without them the in-game button's last choice stands.
        Bundle extras = getIntent() == null ? null : getIntent().getExtras();
        String touch = extras == null ? null : extras.getString(LauncherSettings.EXTRA_TOUCH);
        boolean hidden = touch == null ? preferences.getBoolean("hidden", false) : touch.equals("hide");
        overlay = new TouchOverlay(this, json, hidden, new File(gamePaths.dataDir, TouchOverlay.USER_FILE));
        String opacity = extras == null ? null : extras.getString(LauncherSettings.EXTRA_OPACITY);
        if (opacity != null) {
            try {
                overlay.setAlpha(Math.max(10, Math.min(100, Integer.parseInt(opacity))) / 100f);
            } catch (NumberFormatException e) {
                gamePaths.logLine("java: touch_opacity " + opacity + " is not a number, drawn fully");
            }
        }
        layout.addView(overlay, new ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        if (extras != null && "1".equals(extras.getString(LauncherSettings.EXTRA_TOUCH_EDIT))) {
            overlay.startEditing();   // from the launcher's "Customize touch layout"
            gamePaths.logLine("java: touch layout editor opened from the launcher");
        }
        inputManager = (InputManager) getSystemService(Context.INPUT_SERVICE);
        inputManager.registerInputDeviceListener(deviceListener, null);
        updateControllerPresence("start");
        gamePaths.logLine("java: touch controls added (hidden: " + overlay.isUserHidden() + ", from " + (touch == null ? "the in-game button" : "the launcher: " + touch)
                + ", opacity " + (opacity == null ? "100" : opacity) + "%)");
    }

    void rememberOverlayHidden(boolean hidden) {
        if (preferences != null) preferences.edit().putBoolean("hidden", hidden).apply();
    }

    /** A game controller (a gamepad that is not the phone's own) hides the touch controls; none shows them again. */
    private void updateControllerPresence(String why) {
        if (overlay == null) return;
        String found = null;
        for (int id : InputDevice.getDeviceIds()) {
            InputDevice device = InputDevice.getDevice(id);
            if (device == null || device.isVirtual()) continue;
            int sources = device.getSources();
            boolean pad = (sources & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD;
            boolean external = Build.VERSION.SDK_INT < 34 || device.isExternal();
            if (pad && external) { found = device.getName() + " (id " + id + ")"; break; }
        }
        boolean present = found != null;
        gamePaths.logLine("java: input devices " + why + ": controller " + (present ? found : "none") + ", touch controls " + (present ? "hidden" : "shown unless hidden by choice"));
        overlay.setControllerPresent(present);
    }

    @Override
    protected void onPause() {
        if (overlay != null) overlay.releaseAll();
        super.onPause();
    }

    // ---- Back ----

    private void backShort() {
        if (overlay != null && overlay.isEditing()) {   // in the layout editor Back means Done
            overlay.finishEditing();
            return;
        }
        if (android.os.SystemClock.uptimeMillis() < quitArmedUntil) {
            gamePaths.logLine("java: Back again within the window: leaving the game");
            finish();
            return;
        }
        gamePaths.logLine("java: Back pressed: the remote's +");
        if (overlay != null) overlay.pulse(org.wiirecomp.HostInput.PLUS, 120);
    }

    private void backHeld() {
        quitArmedUntil = android.os.SystemClock.uptimeMillis() + QUIT_WINDOW_MS;
        gamePaths.logLine("java: Back held: press Back again within 3 s to leave");
        Toast.makeText(this, "Press Back once more to leave the game. Unsaved progress is lost.", Toast.LENGTH_LONG).show();
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        // A game controller's own buttons go to SDL (some pads report their B as Back); only the phone's Back is ours.
        if (event.getKeyCode() == KeyEvent.KEYCODE_BACK && (event.getSource() & (InputDevice.SOURCE_GAMEPAD | InputDevice.SOURCE_JOYSTICK)) == 0) {
            if (event.getAction() == KeyEvent.ACTION_DOWN) {
                if (event.getRepeatCount() == 0) { backDownAt = event.getEventTime(); backHoldHandled = false; }
                else if (!backHoldHandled && event.getEventTime() - backDownAt >= BACK_HOLD_MS) { backHoldHandled = true; backHeld(); }
            } else if (event.getAction() == KeyEvent.ACTION_UP) {
                if (!backHoldHandled && !event.isCanceled()) backShort();
            }
            return true;
        }
        return super.dispatchKeyEvent(event);
    }

    @Override
    public void onBackPressed() {
        // Back as a system gesture arrives as a key event too (dispatchKeyEvent above); this only keeps SDL's default away.
    }

    @Override
    protected String[] getLibraries() {
        return new String[] {"SDL3", "wiirecomp"};
    }

    @Override
    protected String getMainFunction() {
        return "wiirecomp_main";
    }

    @Override
    protected String[] getArguments() {
        return launchArguments;
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) hideSystemBars();
    }

    private void hideSystemBars() {
        Window window = getWindow();
        WindowInsetsController controller = window.getInsetsController();
        if (controller != null) {
            controller.hide(WindowInsets.Type.systemBars());
            controller.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
        }
    }

    @Override
    protected void onDestroy() {
        if (inputManager != null) inputManager.unregisterInputDeviceListener(deviceListener);
        super.onDestroy();
        // The runtime starts once per process, so the process goes with the Activity.
        if (isFinishing()) Process.killProcess(Process.myPid());
    }
}
