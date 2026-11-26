package io.github.seaofstarproject.tod;

import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * The launcher's settings, kept in SharedPreferences ("launcher") and handed to the game as Intent extras:
 * the launcher and the game run in different processes, so extras are the only channel. Every default is
 * "not sent" (an empty value), so an untouched launcher starts the game exactly as a plain am start does.
 * Settings that map to a runtime switch use its WII_RECOMP_ name as the extra (GamePaths.environment forwards
 * those to Os.setenv); the touch controls' two settings are read by GameActivity itself.
 */
final class LauncherSettings {
    static final String EXTRA_TOUCH = "touch_controls";    // "show" / "hide"; absent: what the in-game button last chose
    static final String EXTRA_TOUCH_EDIT = "touch_edit";   // "1": open the touch layout editor over the game
    static final String EXTRA_OPACITY = "touch_opacity";   // percent, 10..100

    /** One choice setting: what the user sees, the value stored and sent ("" is the default and is not sent). */
    static final class Choice {
        final String key, title, note, extra;
        final String[] labels, values;

        Choice(String key, String title, String note, String extra, String[] labels, String[] values) {
            this.key = key; this.title = title; this.note = note; this.extra = extra; this.labels = labels; this.values = values;
        }

        int indexOf(String value) {
            for (int i = 0; i < values.length; i++) if (values[i].equals(value)) return i;
            return 0;
        }
    }

    private static final String[] OFF_ON = {"Off (default)", "On"};
    private static final String[] OFF_ON_VALUES = {"", "1"};

    // The windows' own list has 1x, 1.5x, 2x, 3x. WII_RECOMP_SCALE takes whole numbers or fractions ("3/2"), never decimals.
    static final Choice SCALE = new Choice("scale", "Render scale",
            "Internal picture size, times the Wii's 640x480. Default is the runtime's own (2x). Higher costs GPU time.",
            "WII_RECOMP_SCALE",
            new String[] {"Default (2x)", "1x", "1.5x", "2x", "2.5x", "3x"},
            new String[] {"", "1", "3/2", "2", "5/2", "3"});
    static final Choice FSR = new Choice("fsr", "FSR 1.0 upscaler",
            "AMD FidelityFX Super Resolution in the runtime's present pass. Default is off. Not yet seen running on a phone: if the "
                    + "window's pictures can't be drawn into, the runtime logs \"fsr:\" and keeps the plain blit.",
            "WII_RECOMP_FSR",
            new String[] {"Off (default)", "On"},
            new String[] {"", "1"});
    static final Choice PRESENT = new Choice("present", "Present mode",
            "VSync waits for the display; Mailbox shows the newest picture. Immediate is not offered: the tested phone does not list it. "
                    + "A mode the display lacks falls back to VSync (logged).",
            "WII_RECOMP_PRESENT",
            new String[] {"Default (VSync)", "VSync", "Mailbox"},
            new String[] {"", "fifo", "mailbox"});
    static final Choice STRETCH = new Choice("stretch", "Fill the screen",
            "Stretch the picture over the whole window instead of the largest 4:3 (or 16:9) rectangle with bars. Touch pointing follows.",
            "WII_RECOMP_STRETCH", OFF_ON, OFF_ON_VALUES);
    static final Choice FPS = new Choice("fps", "Frame rate overlay",
            "Draws the frames per second in the picture's top left corner (the runtime's own overlay, the same as the Windows switch).",
            "WII_RECOMP_FPS_OVERLAY",
            new String[] {"Off (default)", "On"},
            new String[] {"", "left"});
    static final Choice TOUCH = new Choice("touch", "Touch controls",
            "Remember uses the button in the controls' top right corner. A game controller hides them either way.",
            EXTRA_TOUCH,
            new String[] {"Remember last (default)", "Shown", "Hidden"},
            new String[] {"", "show", "hide"});
    static final Choice OPACITY = new Choice("opacity", "Touch controls opacity",
            "How solid the on-screen pad and buttons are drawn.",
            EXTRA_OPACITY,
            new String[] {"Default (100%)", "75%", "50%", "35%", "20%"},
            new String[] {"", "75", "50", "35", "20"});
    static final Choice MUTE = new Choice("mute", "Sound",
            "Mute switches the runtime's audio output off. There is no volume control in the runtime.",
            "WII_RECOMP_MUTE",
            new String[] {"On (default)", "Muted"},
            new String[] {"", "1"});
    static final Choice AFFINITY = new Choice("affinity", "Thread placement",
            "Which cores the game's two busy threads run on. Default is auto: with exactly two fast cores one thread goes on each. "
                    + "Off leaves it to the scheduler; split forces one per fast core.",
            "WII_RECOMP_AFFINITY",
            new String[] {"Default (auto)", "Off", "Split"},
            new String[] {"", "off", "split"});
    static final Choice HINT = new Choice("hint", "Performance hint",
            "Reports each frame's work time to Android's performance hint (API 33+) so it can raise the clocks. Off stops the reports.",
            "WII_RECOMP_PERF_HINT",
            new String[] {"On (default)", "Off"},
            new String[] {"", "off"});
    static final Choice STATS = new Choice("stats", "Graphics statistics in the log",
            "Counts what the graphics thread does and prints it to logs/run.log. For diagnosis; costs a little time.",
            "WII_RECOMP_GX_STATS", OFF_ON, OFF_ON_VALUES);

    /** In the order the settings screen shows them. */
    static final Choice[] ALL = {SCALE, FSR, PRESENT, STRETCH, FPS, TOUCH, OPACITY, MUTE, AFFINITY, HINT, STATS};

    static final String KEY_SHARPNESS = "sharpness";  // stored in hundredths of the 0..2 range, -1 = untouched
    static final String SHARPNESS_EXTRA = "WII_RECOMP_FSR_SHARPNESS";

    private final SharedPreferences preferences;

    LauncherSettings(Context context) {
        preferences = context.getSharedPreferences("launcher", Context.MODE_PRIVATE);
    }

    String get(Choice choice) {
        String value = preferences.getString(choice.key, "");
        return value == null ? "" : value;
    }

    void set(Choice choice, String value) {
        preferences.edit().putString(choice.key, value).apply();
    }

    /** FSR sharpness in hundredths (0..200, 20 is the runtime's own default), or -1 when untouched. */
    int sharpness() { return preferences.getInt(KEY_SHARPNESS, -1); }

    void setSharpness(int hundredths) { preferences.edit().putInt(KEY_SHARPNESS, hundredths).apply(); }

    void reset() { preferences.edit().clear().apply(); }

    /** Puts every setting that is not at its default on the intent that starts GameActivity. */
    void putExtras(Intent intent) {
        for (Choice choice : ALL) {
            String value = get(choice);
            if (!value.isEmpty()) intent.putExtra(choice.extra, value);
        }
        // The sharpness only means something with FSR on; sending it alone would be a setting that does nothing.
        if (sharpness() >= 0 && !get(FSR).isEmpty()) {
            intent.putExtra(SHARPNESS_EXTRA, String.format(Locale.ROOT, "%.2f", sharpness() / 100.0));
        }
    }

    /** One line per setting that is not at its default, for the launcher's main screen. */
    String summary() {
        List<String> lines = new ArrayList<>();
        for (Choice choice : ALL) {
            String value = get(choice);
            if (!value.isEmpty()) lines.add(choice.title + ": " + choice.labels[choice.indexOf(value)]);
        }
        if (sharpness() >= 0 && !get(FSR).isEmpty()) lines.add(String.format(Locale.ROOT, "FSR sharpness: %.2f", sharpness() / 100.0));
        if (lines.isEmpty()) return "Settings: all at their defaults";
        StringBuilder text = new StringBuilder("Settings (changed from default):");
        for (String line : lines) text.append("\n  ").append(line);
        return text.toString();
    }
}
