package io.github.seaofstarproject.tod;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.graphics.Typeface;
import android.os.Build;
import android.os.Bundle;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.Spinner;
import android.widget.TextView;

import java.io.File;
import java.util.Locale;

/**
 * The launcher, in plain views styled after the Windows launcher (see Ui): what is present, Play, the settings it will start the game
 * with, the last log. Settings are saved at once and sent to the game as extras each time Play is pressed.
 */
public class LauncherActivity extends Activity {
    private GamePaths paths;
    private TextView status;
    private TextView log;
    private Button play;
    private TextView settingsSummary;
    private TextView headline;
    private TextView eyebrow;
    private TextView pill;
    private LauncherSettings settings;
    private ScrollView mainView;
    private ScrollView settingsView;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        paths = new GamePaths(this);
        settings = new LauncherSettings(this);
        mainView = buildMain();
        settingsView = buildSettings();
        setContentView(mainView);
    }

    private int dp(float value) { return Ui.dp(this, value); }

    /** A scrolling page on the sky background that keeps clear of the system bars and the camera cut-out. */
    private ScrollView page(LinearLayout content) {
        ScrollView scroll = new ScrollView(this);
        scroll.setBackground(new Ui.Scenery(this));
        scroll.setFillViewport(true);
        scroll.setClipToPadding(false);
        int pad = dp(16);
        content.setPadding(pad, pad, pad, pad);
        scroll.addView(content, new ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        scroll.setOnApplyWindowInsetsListener((v, insets) -> {
            android.graphics.Insets bars = insets.getInsets(WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
            v.setPadding(bars.left, bars.top, bars.right, bars.bottom);
            return insets;
        });
        return scroll;
    }

    private TextView text(String value, int sizeSp, int color, boolean bold) {
        TextView view = new TextView(this);
        view.setText(value);
        view.setTextSize(sizeSp);
        view.setTextColor(color);
        if (bold) view.setTypeface(Typeface.DEFAULT_BOLD);
        return view;
    }

    private View space(int heightDp) {
        View view = new View(this);
        view.setLayoutParams(new LinearLayout.LayoutParams(1, dp(heightDp)));
        return view;
    }

    /** The title block: brand, big title and a line under it, as the Windows header. */
    private LinearLayout header(String subtitle, View right) {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        LinearLayout titles = new LinearLayout(this);
        titles.setOrientation(LinearLayout.VERTICAL);
        titles.addView(text("SEA OF STAR", 12, Ui.BRAND_TEXT, true));
        Ui.OutlinedText title = new Ui.OutlinedText(this, Ui.TITLE_OUTLINE, dp(2.5f));
        title.setText("Tides of Destiny");
        title.setTextSize(32);
        title.setPadding(dp(2.5f), 0, dp(2.5f), dp(5));
        titles.addView(title);
        titles.addView(text(subtitle, 12, Ui.BG_TEXT, false));
        row.addView(titles, Ui.weighted(1));
        if (right != null) row.addView(right);
        return row;
    }

    private ScrollView buildMain() {
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);

        pill = Ui.pill(this, "", Ui.PILL_OK);
        root.addView(header("Static recompilation \u00B7 native Android arm64 \u00B7 disc SO3EE9", pill));
        root.addView(space(14));

        // The hero card: what the state is, Play as the one big action, Settings and Refresh beside it.
        LinearLayout hero = Ui.card(this);
        eyebrow = text("", 11, Ui.WARN, true);
        hero.addView(eyebrow);
        headline = text("Native Android arm64", 22, Ui.CARD_TEXT, true);
        hero.addView(headline);
        hero.addView(text("Rune Factory: Tides of Destiny, rebuilt from your own disc.", 13, Ui.CARD_DIM, false));
        hero.addView(space(12));
        LinearLayout buttons = new LinearLayout(this);
        buttons.setOrientation(LinearLayout.HORIZONTAL);
        buttons.setGravity(Gravity.CENTER_VERTICAL);
        play = Ui.button(this, "\u25B6  Play", Ui.GREEN, true);
        play.setOnClickListener(v -> {
            Intent intent = new Intent(this, GameActivity.class);
            settings.putExtras(intent);
            startActivity(intent);
        });
        Button settingsButton = Ui.button(this, "Settings", Ui.GOLD, false);
        settingsButton.setOnClickListener(v -> showSettings(true));
        Button refresh = Ui.button(this, "Refresh", Ui.GOLD, false);
        refresh.setOnClickListener(v -> refresh());
        buttons.addView(play);
        LinearLayout.LayoutParams gap = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        gap.leftMargin = dp(12);
        buttons.addView(settingsButton, gap);
        buttons.addView(refresh, gap);
        hero.addView(buttons);
        hero.addView(space(12));
        settingsSummary = text("", 12, Ui.CARD_DIM, false);
        settingsSummary.setTypeface(Typeface.MONOSPACE);
        hero.addView(settingsSummary);

        // What is present.
        LinearLayout files = Ui.card(this);
        files.addView(Ui.tab(this, "GAME FILES"), new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        files.addView(space(8));
        status = text("", 11, Ui.CARD_DIM, false);
        status.setTypeface(Typeface.MONOSPACE);
        files.addView(status);

        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        LinearLayout.LayoutParams left = Ui.weighted(1), right = Ui.weighted(1);
        left.rightMargin = dp(8);
        right.leftMargin = dp(8);
        row.addView(hero, left);
        row.addView(files, right);
        root.addView(row);
        root.addView(space(14));

        // The last log, in a dark console as on the Windows Build page.
        LinearLayout console = new LinearLayout(this);
        console.setOrientation(LinearLayout.VERTICAL);
        console.setBackground(Ui.round(this, Ui.CONSOLE, Ui.CONSOLE, Ui.CONSOLE_BORDER, 2.5f, 16));
        console.setPadding(dp(14), dp(10), dp(14), dp(12));
        console.addView(text("LAST LOG", 11, Ui.LOG_HEADER, true));
        log = text("", 10, Ui.LOG_TEXT, false);
        log.setTypeface(Typeface.MONOSPACE);
        log.setTextIsSelectable(true);
        console.addView(log);
        root.addView(console);
        return page(root);
    }

    private void showSettings(boolean show) {
        setContentView(show ? settingsView : mainView);
        if (show) settingsView.scrollTo(0, 0);
        else refresh();
    }

    @Override
    public void onBackPressed() {
        if (settingsView.getParent() != null) showSettings(false);
        else super.onBackPressed();
    }

    /** One setting: its name and note on the left, the drop-down on the right, in a recessed slot. */
    private LinearLayout settingRow(LauncherSettings.Choice choice) {
        LinearLayout slot = Ui.slot(this);
        LinearLayout line = new LinearLayout(this);
        line.setOrientation(LinearLayout.HORIZONTAL);
        line.setGravity(Gravity.CENTER_VERTICAL);
        LinearLayout names = new LinearLayout(this);
        names.setOrientation(LinearLayout.VERTICAL);
        names.addView(text(choice.title, 15, Ui.CARD_TEXT, false));
        names.addView(text(choice.note, 11, Ui.CARD_MUTED, false));
        line.addView(names, Ui.weighted(1));
        Spinner spinner = spinnerFor(choice);
        LinearLayout.LayoutParams sp = new LinearLayout.LayoutParams(dp(170), dp(40));
        sp.leftMargin = dp(10);
        line.addView(spinner, sp);
        slot.addView(line);
        if (choice == LauncherSettings.FSR) slot.addView(sharpnessRow());
        return slot;
    }

    /** Customize touch layout (starts the game with the editor open) and Reset touch layout (deletes the player's layout file). */
    private LinearLayout touchLayoutRow() {
        LinearLayout box = new LinearLayout(this);
        box.setOrientation(LinearLayout.VERTICAL);
        TextView note = text("", 11, Ui.CARD_MUTED, false);
        File userLayout = new File(paths.dataDir, TouchOverlay.USER_FILE);
        note.setText("Drag, resize or move any control over the running game. " + (userLayout.isFile() ? "A custom layout is saved." : "Using the default layout."));
        Button customize = Ui.button(this, "Customize touch layout", Ui.GOLD, false);
        customize.setOnClickListener(v -> {
            Intent intent = new Intent(this, GameActivity.class);
            settings.putExtras(intent);
            intent.putExtra(LauncherSettings.EXTRA_TOUCH, "show");
            intent.putExtra(LauncherSettings.EXTRA_TOUCH_EDIT, "1");
            startActivity(intent);
        });
        Button reset = Ui.button(this, "Reset touch layout", Ui.GOLD, false);
        reset.setOnClickListener(v -> {
            boolean had = userLayout.isFile();
            boolean gone = !had || userLayout.delete();
            note.setText(!gone ? "Could not delete " + userLayout.getPath() : had ? "Custom layout deleted: the default layout is used." : "Already using the default layout.");
        });
        LinearLayout buttons = new LinearLayout(this);
        buttons.setOrientation(LinearLayout.HORIZONTAL);
        buttons.addView(customize);
        LinearLayout.LayoutParams gap = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        gap.leftMargin = dp(12);
        buttons.addView(reset, gap);
        box.addView(buttons);
        box.addView(space(6));
        box.addView(note);
        return box;
    }

    /** The settings in cards, grouped as before; every change is saved at once. */
    private ScrollView buildSettings() {
        LinearLayout column = new LinearLayout(this);
        column.setOrientation(LinearLayout.VERTICAL);

        LinearLayout buttons = new LinearLayout(this);
        buttons.setOrientation(LinearLayout.HORIZONTAL);
        buttons.setGravity(Gravity.CENTER_VERTICAL);
        Button back = Ui.button(this, "Back", Ui.GOLD, false);
        back.setOnClickListener(v -> showSettings(false));
        buttons.addView(back);
        buttons.addView(new View(this), Ui.weighted(1));
        Button reset = Ui.button(this, "Reset to defaults", Ui.GOLD, false);
        reset.setOnClickListener(v -> {
            settings.reset();
            settingsView = buildSettings();
            setContentView(settingsView);
        });
        Button done = Ui.button(this, "Done", Ui.GREEN, true);
        done.setMinimumWidth(dp(120));
        done.setOnClickListener(v -> showSettings(false));
        buttons.addView(reset);
        LinearLayout.LayoutParams gap = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        gap.leftMargin = dp(12);
        buttons.addView(done, gap);

        column.addView(header("Saved on this phone and sent to the game each time you press Play. Defaults send nothing, so the runtime's own choices apply.", null));
        column.addView(space(10));
        TextView heading = new Ui.OutlinedText(this, Ui.TITLE_OUTLINE, dp(2));
        heading.setText("Settings");
        heading.setTextSize(22);
        column.addView(heading);
        column.addView(space(8));

        LauncherSettings.Choice[][] sections = {
                {LauncherSettings.SCALE, LauncherSettings.FSR, LauncherSettings.PRESENT, LauncherSettings.STRETCH},
                {LauncherSettings.FPS},
                {LauncherSettings.TOUCH, LauncherSettings.OPACITY},
                {LauncherSettings.MUTE},
                {LauncherSettings.AFFINITY, LauncherSettings.HINT, LauncherSettings.STATS}};
        String[] headings = {"PICTURE", "IN GAME", "TOUCH CONTROLS", "AUDIO", "PERFORMANCE (ANDROID)"};
        // Two columns of cards, as the Windows Settings page: picture, in game and audio on the left, the rest on the right.
        int[] columnOf = {0, 0, 1, 0, 1};
        LinearLayout[] columns = {new LinearLayout(this), new LinearLayout(this)};
        for (LinearLayout c : columns) c.setOrientation(LinearLayout.VERTICAL);
        for (int s = 0; s < sections.length; s++) {
            LinearLayout card = Ui.card(this);
            card.addView(Ui.tab(this, headings[s]), new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT));
            for (LauncherSettings.Choice choice : sections[s]) {
                card.addView(space(8));
                card.addView(settingRow(choice));
            }
            if (s == 2) {
                card.addView(space(10));
                card.addView(touchLayoutRow());
            }
            LinearLayout col = columns[columnOf[s]];
            if (col.getChildCount() > 0) col.addView(space(12));
            col.addView(card);
        }
        LinearLayout notOn = Ui.card(this);
        notOn.addView(Ui.tab(this, "NOT ON ANDROID"), new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        notOn.addView(space(6));
        notOn.addView(text("Window size and Fullscreen: the phone's screen is the window, always full. Frame limit: the Windows "
                + "launcher has none either (30 fps is the game's own pace; present mode only chooses how pictures are shown).", 11, Ui.CARD_MUTED, false));
        columns[1].addView(space(12));
        columns[1].addView(notOn);

        LinearLayout grid = new LinearLayout(this);
        grid.setOrientation(LinearLayout.HORIZONTAL);
        LinearLayout.LayoutParams left = Ui.weighted(1), right = Ui.weighted(1);
        left.rightMargin = dp(8);
        right.leftMargin = dp(8);
        left.gravity = Gravity.TOP;
        right.gravity = Gravity.TOP;
        grid.addView(columns[0], left);
        grid.addView(columns[1], right);
        column.addView(grid);
        column.addView(space(14));
        column.addView(buttons);
        return page(column);
    }

    /** A drop-down in the page's colours: the chosen value with a small arrow, the list on parchment. */
    private Spinner spinnerFor(LauncherSettings.Choice choice) {
        Spinner spinner = new Spinner(this);
        spinner.setBackground(Ui.round(this, 0xFFFFF4CC, 0xFFF0D8A0, Ui.ROW_BORDER, 2, 10));
        spinner.setPopupBackgroundDrawable(Ui.round(this, Ui.PANEL_TOP, Ui.PANEL_BOTTOM, Ui.PANEL_OUTLINE, 2, 10));
        spinner.setPadding(dp(8), 0, dp(8), 0);
        spinner.setAdapter(new ArrayAdapter<String>(this, android.R.layout.simple_spinner_dropdown_item, choice.labels) {
            private TextView styled(TextView v, boolean arrow, int position) {
                v.setTextColor(Ui.CARD_TEXT);
                v.setTextSize(13);
                v.setSingleLine(true);
                v.setGravity(Gravity.CENTER_VERTICAL);
                v.setText(choice.labels[position] + (arrow ? "  \u25BE" : ""));
                return v;
            }
            @Override public View getView(int position, View convertView, ViewGroup parent) {
                return styled((TextView) super.getView(position, convertView, parent), true, position);
            }
            @Override public View getDropDownView(int position, View convertView, ViewGroup parent) {
                TextView v = styled((TextView) super.getDropDownView(position, convertView, parent), false, position);
                v.setBackgroundColor(0);
                v.setMinHeight(dp(44));
                return v;
            }
        });
        spinner.setSelection(choice.indexOf(settings.get(choice)));
        spinner.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override public void onItemSelected(AdapterView<?> parent, View view, int position, long id) {
                settings.set(choice, choice.values[position]);
            }
            @Override public void onNothingSelected(AdapterView<?> parent) { }
        });
        return spinner;
    }

    /** FSR's RCAS strength, 0 (sharpest) to 2 in 0.05 steps; the runtime's own default is 0.20 until it is moved. */
    private LinearLayout sharpnessRow() {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.VERTICAL);
        TextView value = text("", 13, Ui.CARD_TEXT, false);
        value.setPadding(0, dp(8), 0, 0);
        SeekBar bar = new SeekBar(this);
        bar.setMax(40);
        int stored = settings.sharpness();
        bar.setProgress(stored < 0 ? 4 : stored / 5);
        value.setText(sharpnessText(stored < 0 ? 20 : stored));
        bar.setProgressTintList(android.content.res.ColorStateList.valueOf(0xFF3EB02C));
        bar.setThumbTintList(android.content.res.ColorStateList.valueOf(0xFF22761E));
        bar.setProgressBackgroundTintList(android.content.res.ColorStateList.valueOf(0xFFC4A876));
        bar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar b, int progress, boolean fromUser) {
                value.setText(sharpnessText(progress * 5));
                if (fromUser) settings.setSharpness(progress * 5);
            }
            @Override public void onStartTrackingTouch(SeekBar b) { }
            @Override public void onStopTrackingTouch(SeekBar b) { }
        });
        row.addView(value);
        row.addView(bar);
        row.addView(text("FSR sharpness (RCAS), 0 is sharpest. Sent only while FSR is on.", 11, Ui.CARD_MUTED, false));
        return row;
    }

    private static String sharpnessText(int hundredths) {
        return String.format(Locale.ROOT, "FSR sharpness: %.2f", hundredths / 100.0);
    }

    @Override
    protected void onResume() {
        super.onResume();
        refresh();
    }

    private static String describe(File file) {
        return file.isFile() ? String.format(Locale.ROOT, "yes (%,d bytes)", file.length()) : "no";
    }

    private void refresh() {
        paths.ensureDirs();
        File sys = paths.treeSys();
        boolean runtime = paths.libRuntime().isFile() && paths.libSdl().isFile();
        boolean data = paths.hasImage() || sys != null;
        StringBuilder text = new StringBuilder();
        text.append("Data folder: ").append(paths.dataDir).append(paths.external ? "" : "  (no external storage)").append('\n');
        text.append("Image SO3EE9.wrd: ").append(describe(paths.image)).append('\n');
        text.append("Extracted tree: ").append(sys == null ? "no" : sys.getPath()).append('\n');
        text.append("libwiirecomp.so: ").append(describe(paths.libRuntime())).append('\n');
        text.append("libSDL3.so: ").append(describe(paths.libSdl())).append('\n');
        text.append("libtitle.so (APK): ").append(describe(paths.libTitle())).append("  (an image carries its own)\n");
        text.append("Device ABI: ").append(Build.SUPPORTED_ABIS[0]).append(", Android ").append(Build.VERSION.RELEASE).append('\n');
        if (!runtime) text.append("Runtime libraries are missing: build the APK with -PnativeLibsDir.\n");
        else if (!data) text.append("No game data: run tools\\android-push.ps1.\n");
        status.setText(text);
        boolean ready = runtime && data;
        play.setEnabled(ready);
        eyebrow.setText(ready ? "READY TO PLAY" : "NOT READY");
        eyebrow.setTextColor(ready ? Ui.WARN : Ui.ACCENT);
        pill.setText(ready ? "\u25CF  READY" : "\u25CF  NOT READY");
        pill.setBackground(Ui.round(this, ready ? Ui.PILL_OK : Ui.PILL_BAD, ready ? Ui.PILL_OK : Ui.PILL_BAD, ready ? 0xFF2E7A1C : 0xFFA03020, 2, 14));
        settingsSummary.setText(settings.summary());
        log.setText(paths.tailLog(60));
    }
}
