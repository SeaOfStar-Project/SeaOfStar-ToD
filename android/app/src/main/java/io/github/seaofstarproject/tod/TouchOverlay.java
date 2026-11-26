package io.github.seaofstarproject.tod;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Rect;
import android.graphics.RectF;
import android.util.DisplayMetrics;
import android.util.Log;
import android.util.SparseArray;
import android.view.DisplayCutout;
import android.view.HapticFeedbackConstants;
import android.view.MotionEvent;
import android.view.ViewConfiguration;
import android.view.View;
import android.view.WindowInsets;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;
import org.wiirecomp.HostInput;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * The on-screen Wii Remote and Nunchuk, drawn over the game's surface. It takes every touch that reaches it:
 * a floating stick (the Nunchuk's) in the left part of the screen, buttons and a D-pad where the layout puts them,
 * and a touch anywhere else is the pointer (mapped onto the game's picture by the runtime, which knows where the
 * letterbox is). Several fingers at once; a finger sliding from one button to another presses the second.
 * What it shows comes from a layout (touch_layout.json, sizes in dp); see assets/touch_layout.json.
 * Everything the game sees goes through HostInput (the runtime's own virtual input).
 */
final class TouchOverlay extends View {
    private static final String TAG = "SeaOfStar";
    private static final int PLAYER = 0;
    private static final int ROLE_NONE = 0, ROLE_STICK = 1, ROLE_BUTTON = 2, ROLE_POINTER = 3, ROLE_TOGGLE = 4;
    private static final float DEADZONE = 0.1f;
    /** The player's own layout, on top of the built-in one: a small JSON file in the game's data folder. */
    static final String USER_FILE = "touch_layout_user.json";
    private static final float MIN_SCALE = 0.5f, MAX_SCALE = 2.0f;

    /** One thing to press: a circle or a rounded rectangle at a place on the screen. */
    private static final class Item {
        String id, label;
        String arcOf;                    // when set, the centre comes from ArcLayout: around the item with this id
        float arcAngle, arcDist;         // preferred angle (degrees, up is 90) and centre distance (dp) from that item
        int mask;
        boolean circle, arm;
        String anchor;
        float dx, dy, r, w, h, text;     // dp from the anchor to the centre
        float ox, oy;                    // dp, added on the screen after the anchor (a D-pad arm's offset from its centre; y grows down)
        float cx, cy;                    // pixels, set by place()
        float sc = 1;                    // the player's size factor for this control (the layout editor)
        boolean fixed;                   // the player put it somewhere: fx, fy are fractions of the window, and the anchor and arc are ignored
        float fx, fy;
        /** r, w and h are dp; x, y and slop are pixels. */
        boolean hit(float x, float y, float slop, float density) {
            if (circle) { float ex = x - cx, ey = y - cy, rr = r * sc * density + slop; return ex * ex + ey * ey <= rr * rr; }
            return Math.abs(x - cx) <= w * sc * density / 2 + slop && Math.abs(y - cy) <= h * sc * density / 2 + slop;
        }
    }

    private static final class Touch {
        int role = ROLE_NONE;
        int item = -1;
        float originX, originY, x, y;
    }

    private final float density;
    private final List<Item> items = new ArrayList<>();
    private final SparseArray<Touch> touches = new SparseArray<>();
    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG), line = new Paint(Paint.ANTI_ALIAS_FLAG), text = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF rect = new RectF();
    private final int[] pictureOut = new int[4];

    // Layout, from the JSON.
    private float stickRadiusDp = 56, stickRestX = 108, stickRestY = 98;
    private String stickAnchor = "bl";
    private float zoneLeft = 0, zoneRight = 0.4f, zoneTop = 0.15f, zoneBottom = 1;
    private Item toggle;
    private float stickScale = 1;
    private boolean stickFixed;
    private float stickFx, stickFy;
    private final Map<String, float[]> user = new LinkedHashMap<>();   // id -> {x, y (fractions of the window, NaN: not moved), scale (NaN: 1)}
    private final File userFile;
    private float stickRestPx, stickRestPy;
    private float pictureAspect = 16f / 9f;
    private final List<float[]> avoidFractions = new ArrayList<>();   // picture-relative {l, t, r, b} the arc buttons keep off (the game's HUD)

    private boolean userHidden, controllerPresent;
    private int held;          // the buttons pressed by fingers now
    private int pulsed;        // a button the app presses for a moment (Back is +)
    private float stickX, stickY;
    private int sentButtons = -1;
    private float sentStickX = 2, sentStickY = 2;
    private int safeLeft, safeRight, safeTop;
    private boolean logLayout = true;   // log every control's final place once after the next layout pass (size or insets changed)
    private static final String LOG = "TouchOverlay";

    TouchOverlay(Context context, String layoutJson, boolean hidden, File userFile) {
        super(context);
        this.userFile = userFile;
        density = context.getResources().getDisplayMetrics().density;
        userHidden = hidden;
        setFocusable(false);
        setFocusableInTouchMode(false);
        setWillNotDraw(false);
        setHapticFeedbackEnabled(true);
        line.setStyle(Paint.Style.STROKE);
        line.setStrokeWidth(2 * density);
        text.setTextAlign(Paint.Align.CENTER);
        text.setFakeBoldText(true);
        parse(layoutJson);
        loadUser();
        applyUser();
    }

    // ---- layout ----

    private void parse(String json) {
        try {
            JSONObject root = new JSONObject(json);
            JSONObject stick = root.optJSONObject("stick");
            if (stick != null) {
                JSONObject zone = stick.optJSONObject("zone");
                if (zone != null) {
                    zoneLeft = (float) zone.optDouble("left", zoneLeft);
                    zoneRight = (float) zone.optDouble("right", zoneRight);
                    zoneTop = (float) zone.optDouble("top", zoneTop);
                    zoneBottom = (float) zone.optDouble("bottom", zoneBottom);
                }
                stickRadiusDp = (float) stick.optDouble("radius", stickRadiusDp);
                JSONObject rest = stick.optJSONObject("rest");
                if (rest != null) {
                    stickAnchor = rest.optString("anchor", stickAnchor);
                    stickRestX = (float) rest.optDouble("x", stickRestX);
                    stickRestY = (float) rest.optDouble("y", stickRestY);
                }
            }
            JSONObject picture = root.optJSONObject("picture");
            if (picture != null) {
                pictureAspect = (float) picture.optDouble("aspect", pictureAspect);
                JSONArray avoid = picture.optJSONArray("avoid");
                for (int i = 0; avoid != null && i < avoid.length(); i++) {
                    JSONArray r = avoid.getJSONArray(i);
                    avoidFractions.add(new float[] {(float) r.getDouble(0), (float) r.getDouble(1), (float) r.getDouble(2), (float) r.getDouble(3)});
                }
            }
            JSONObject tg = root.optJSONObject("toggle");
            if (tg != null) {
                toggle = new Item();
                toggle.id = "toggle";
                toggle.circle = true;
                toggle.anchor = tg.optString("anchor", "tr");
                toggle.dx = (float) tg.optDouble("x", 24);
                toggle.dy = (float) tg.optDouble("y", 24);
                toggle.r = (float) tg.optDouble("r", 16);
            }
            JSONArray list = root.optJSONArray("elements");
            for (int i = 0; list != null && i < list.length(); i++) {
                JSONObject e = list.getJSONObject(i);
                String type = e.optString("type", "circle");
                if (type.equals("dpad")) {
                    float arm = (float) e.optDouble("arm", 34), gap = (float) e.optDouble("gap", 36);
                    String[] names = {"up", "down", "left", "right"};
                    float[][] at = {{0, -gap}, {0, gap}, {-gap, 0}, {gap, 0}};  // on the screen: up is -y
                    String[] labels = {"\u25B2", "\u25BC", "\u25C0", "\u25B6"};
                    for (int k = 0; k < 4; k++) {
                        Item it = new Item();
                        it.arm = true;
                        it.id = "dpad";
                        it.anchor = e.optString("anchor", "bl");
                        it.dx = (float) e.optDouble("x", 64);
                        it.dy = (float) e.optDouble("y", 200);
                        it.ox = at[k][0];
                        it.oy = at[k][1];
                        it.w = it.h = arm;
                        it.label = labels[k];
                        it.mask = maskOf(names[k]);
                        it.text = 14;
                        items.add(it);
                    }
                    continue;
                }
                Item it = new Item();
                it.circle = type.equals("circle");
                it.id = e.optString("id", "");
                if (it.id.isEmpty()) it.id = e.optString("button", "");
                JSONObject arc = e.optJSONObject("arc");
                if (arc != null && it.circle) {
                    it.arcOf = arc.optString("of", "");
                    it.arcAngle = (float) arc.optDouble("angle", 135);
                    it.arcDist = (float) arc.optDouble("dist", 76);
                }
                it.label = e.optString("label", "");
                it.mask = maskOf(e.optString("button", ""));
                it.anchor = e.optString("anchor", "br");
                it.dx = (float) e.optDouble("x", 0);
                it.dy = (float) e.optDouble("y", 0);
                it.r = (float) e.optDouble("r", 28);
                it.w = (float) e.optDouble("w", 50);
                it.h = (float) e.optDouble("h", 30);
                it.text = (float) e.optDouble("text", 16);
                if (it.mask != 0) items.add(it);
            }
        } catch (JSONException e) {
            Log.e(TAG, "touch layout: " + e);
            items.clear();
        }
    }

    static int maskOf(String name) {
        switch (name) {
            case "left": return HostInput.LEFT;
            case "right": return HostInput.RIGHT;
            case "down": return HostInput.DOWN;
            case "up": return HostInput.UP;
            case "plus": return HostInput.PLUS;
            case "minus": return HostInput.MINUS;
            case "one": return HostInput.ONE;
            case "two": return HostInput.TWO;
            case "a": return HostInput.A;
            case "b": return HostInput.B;
            case "home": return HostInput.HOME;
            case "c": return HostInput.NUNCHUK_C;
            case "z": return HostInput.NUNCHUK_Z;
            default: return 0;
        }
    }

    private float[] corner(String anchor) {
        int w = getWidth(), h = getHeight();
        switch (anchor) {
            case "tl": return new float[] {safeLeft, safeTop, 1, 1};
            case "tr": return new float[] {w - safeRight, safeTop, -1, 1};
            case "tc": return new float[] {w / 2f, safeTop, 1, 1};
            case "bl": return new float[] {safeLeft, h, 1, -1};
            default: return new float[] {w - safeRight, h, -1, -1};
        }
    }

    private void place(Item it) {
        int w = getWidth(), h = getHeight();
        if (it.fixed) {
            it.cx = Math.max(safeLeft, Math.min(it.fx * w, w - safeRight));
            it.cy = Math.max(0, Math.min(it.fy * h, h));
        } else if (it.anchor.equals("mirror")) {
            // The cluster's centre is the stick's resting place mirrored across the screen; x and y are an offset from it (right and down).
            float margin = 4 * density, rr = it.r * it.sc * density;
            it.cx = Math.min(w - stickRestPx + it.dx * density, w - safeRight - rr - margin);
            it.cy = Math.min(stickRestPy + it.dy * density, h - rr - margin);
        } else {
            float[] c = corner(it.anchor);
            boolean center = it.anchor.equals("tc");
            it.cx = c[0] + (center ? it.dx : c[2] * it.dx) * density;
            it.cy = c[1] + c[3] * it.dy * density;
        }
        // The arm's own offset is a screen direction, whatever corner the pad is measured from.
        it.cx += it.ox * it.sc * density;
        it.cy += it.oy * it.sc * density;
    }

    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);
        logLayout = true;
        readInsets(getRootWindowInsets());
        placeAll();
        // The edges of the screen are the system's back gesture; keep the stick's and the buttons' sides ours.
        int strip = (int) (28 * density);
        List<Rect> keep = new ArrayList<>();
        keep.add(new Rect(0, h / 4, strip, h));
        keep.add(new Rect(w - strip, h / 4, w, h));
        setSystemGestureExclusionRects(keep);
    }

    @Override
    public WindowInsets onApplyWindowInsets(WindowInsets insets) {
        readInsets(insets);
        logLayout = true;
        placeAll();
        invalidate();
        return super.onApplyWindowInsets(insets);
    }

    private void readInsets(WindowInsets insets) {
        safeLeft = safeRight = safeTop = 0;
        if (insets == null) return;
        DisplayCutout cutout = insets.getDisplayCutout();
        if (cutout != null) {
            safeLeft = cutout.getSafeInsetLeft();
            safeRight = cutout.getSafeInsetRight();
            safeTop = cutout.getSafeInsetTop();
        }
    }

    private void placeAll() {
        if (stickFixed) {
            stickRestPx = Math.max(safeLeft, Math.min(stickFx * getWidth(), getWidth() - safeRight));
            stickRestPy = Math.max(0, Math.min(stickFy * getHeight(), getHeight()));
        } else {
            float[] c = corner(stickAnchor);
            stickRestPx = c[0] + c[2] * stickRestX * density;
            stickRestPy = c[1] + c[3] * stickRestY * density;
        }
        for (Item it : items) place(it);
        if (toggle != null) place(toggle);
        placeArcs();
        if (logLayout && getWidth() > 0 && getHeight() > 0) { logLayout = false; logPlaces(); }
    }

    /** One line per control with its final rectangle (pixels), size factor and alpha, so a missing control can be told from logcat. */
    void logPlaces() {
        int w = getWidth(), h = getHeight();
        Log.i(LOG, "layout " + w + "x" + h + " density " + density + " safe l" + safeLeft + " r" + safeRight + " t" + safeTop
                + " overlay alpha " + getAlpha() + " shown " + controlsShown() + " items " + items.size() + " toggle " + (toggle != null));
        RectF r = new RectF();
        List<String> ids = new ArrayList<>();
        ids.add("stick");
        ids.add("dpad");
        if (toggle != null) ids.add("toggle");
        for (Item it : items) if (!it.arm && it.id != null && !it.id.isEmpty()) ids.add(it.id);
        for (String id : ids) {
            boundsOf(id, r);
            float sc = id.equals("stick") ? stickScale : id.equals("dpad") ? dpadScale() : itemById(id) == null ? 0 : itemById(id).sc;
            boolean inside = r.left >= 0 && r.top >= 0 && r.right <= w && r.bottom <= h && r.width() > 0 && r.height() > 0;
            Log.i(LOG, id + " rect [" + Math.round(r.left) + "," + Math.round(r.top) + "," + Math.round(r.right) + "," + Math.round(r.bottom) + "] "
                    + Math.round(r.width()) + "x" + Math.round(r.height()) + " scale " + sc + " alpha " + getAlpha()
                    + (inside ? "" : " OUTSIDE-OR-EMPTY"));
        }
    }

    private float dpadScale() {
        for (Item it : items) if (it.arm) return it.sc;
        return 0;
    }

    /** Whether the runtime stretches the picture over the whole window (the launcher's "Fill the screen"). */
    private boolean pictureStretched() {
        if (!(getContext() instanceof Activity)) return false;
        Intent intent = ((Activity) getContext()).getIntent();
        String value = intent == null ? null : intent.getStringExtra("WII_RECOMP_STRETCH");
        return value != null && !value.isEmpty() && !value.equals("0");
    }

    /**
     * The face-button cluster: buttons that name another one (`arc`) are put around it, from the picture's real rectangle in this
     * window, keeping off the HUD rectangles of the layout. On a 20:9 phone that leaves them in the black bar beside the picture.
     */
    private void placeArcs() {
        int w = getWidth(), h = getHeight();
        if (w == 0 || h == 0) return;
        float[] picture = ArcLayout.picture(w, h, pictureAspect, pictureStretched());
        List<float[]> avoid = new ArrayList<>();
        for (float[] f : avoidFractions) avoid.add(ArcLayout.inPicture(picture, f));
        if (toggle != null) { float tr = toggle.r * toggle.sc * density; avoid.add(new float[] {toggle.cx - tr, toggle.cy - tr, toggle.cx + tr, toggle.cy + tr}); }
        float margin = 4 * density;
        float[] bounds = {safeLeft + margin, margin, w - safeRight - margin, h - margin};
        for (Item origin : items) {
            if (origin.id == null || origin.id.isEmpty() || !origin.circle) continue;
            List<Item> around = new ArrayList<>();
            List<ArcLayout.Disc> discs = new ArrayList<>();
            for (Item it : items) {
                if (!origin.id.equals(it.arcOf) || it.fixed) continue;
                around.add(it);
                discs.add(new ArcLayout.Disc(it.r * it.sc * density, it.arcAngle, it.arcDist * density));
            }
            if (around.isEmpty()) continue;
            ArcLayout.Disc o = new ArcLayout.Disc(origin.r * origin.sc * density, 0, 0);
            o.x = origin.cx;
            o.y = origin.cy;
            ArcLayout.arrange(o, discs, avoid.toArray(new float[0][]), bounds, 3 * density, 10 * density);
            for (int i = 0; i < around.size(); i++) { around.get(i).cx = discs.get(i).x; around.get(i).cy = discs.get(i).y; }
        }
    }

    // ---- state the Activity sets ----

    boolean isUserHidden() { return userHidden; }

    void setUserHidden(boolean hidden) {
        if (userHidden == hidden) return;
        userHidden = hidden;
        if (hidden) releaseAll();
        invalidate();
    }

    void setControllerPresent(boolean present) {
        if (controllerPresent == present) return;
        controllerPresent = present;
        if (present) releaseAll();
        invalidate();
    }

    private boolean controlsShown() { return editing || (!userHidden && !controllerPresent); }

    /** Presses a button for a moment on the app's behalf (Back is +), on the UI thread. */
    void pulse(int mask, long milliseconds) {
        pulsed |= mask;
        sendButtons();
        postDelayed(() -> { pulsed &= ~mask; sendButtons(); }, milliseconds);
    }

    /** Lets go of everything: the app is pausing, the controls are hidden, or the system took the touches. */
    void releaseAll() {
        removeCallbacks(openEditor);
        toggleTouch = -1;
        touches.clear();
        held = 0;
        stickX = stickY = 0;
        sendButtons();
        sendStick();
        invalidate();
    }

    // ---- sending ----

    private void sendButtons() {
        int mask = held | pulsed;
        if (mask == sentButtons) return;
        sentButtons = mask;
        HostInput.setButtons(PLAYER, mask);
    }

    private void sendStick() {
        if (stickX == sentStickX && stickY == sentStickY) return;
        sentStickX = stickX;
        sentStickY = stickY;
        HostInput.setStick(PLAYER, stickX, stickY);
    }

    // ---- touch ----

    private void tick() {
        performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY, HapticFeedbackConstants.FLAG_IGNORE_GLOBAL_SETTING);
    }

    private int hitItem(float x, float y) {
        float slop = 6 * density;
        int best = -1;
        float bestDistance = Float.MAX_VALUE;
        for (int i = 0; i < items.size(); i++) {
            Item it = items.get(i);
            if (!it.hit(x, y, slop, density)) continue;
            float d = (x - it.cx) * (x - it.cx) + (y - it.cy) * (y - it.cy);
            if (d < bestDistance) { best = i; bestDistance = d; }
        }
        return best;
    }

    private boolean inStickZone(float x, float y) {
        int w = getWidth(), h = getHeight();
        float left = zoneLeft * w, right = zoneRight * w;
        if (stickFixed) { left = stickRestPx - 0.2f * w; right = stickRestPx + 0.2f * w; }   // the player moved the stick: its zone goes along
        return x >= left && x <= right && y >= zoneTop * h && y <= zoneBottom * h;
    }

    private boolean stickInUse() {
        for (int i = 0; i < touches.size(); i++) if (touches.valueAt(i).role == ROLE_STICK) return true;
        return false;
    }

    private void recomputeHeld() {
        int mask = 0;
        for (int i = 0; i < touches.size(); i++) {
            Touch t = touches.valueAt(i);
            if (t.role == ROLE_BUTTON && t.item >= 0) mask |= items.get(t.item).mask;
        }
        held = mask;
    }

    private void pointTo(float x, float y) {
        HostInput.setPointer(PLAYER, x, y, getWidth(), getHeight());
    }

    private void down(int id, float x, float y) {
        Touch t = new Touch();
        t.x = x;
        t.y = y;
        touches.put(id, t);
        if (toggle != null && !controllerPresent && toggle.hit(x, y, 8 * density, density)) {
            // A tap hides or shows the controls (on release); holding the eye opens the layout editor.
            t.role = ROLE_TOGGLE;
            toggleTouch = id;
            postDelayed(openEditor, ViewConfiguration.getLongPressTimeout() + 150);
            return;
        }
        if (controlsShown()) {
            int item = hitItem(x, y);
            if (item >= 0) {
                t.role = ROLE_BUTTON;
                t.item = item;
                recomputeHeld();
                tick();
                sendButtons();
                invalidate();
                return;
            }
            if (!stickInUse() && inStickZone(x, y)) {
                t.role = ROLE_STICK;
                t.originX = x;
                t.originY = y;
                stickX = stickY = 0;
                invalidate();
                return;
            }
        }
        t.role = ROLE_POINTER;
        pointTo(x, y);
    }

    private void move(int id, float x, float y) {
        Touch t = touches.get(id);
        if (t == null) return;
        t.x = x;
        t.y = y;
        switch (t.role) {
            case ROLE_BUTTON: {
                int item = hitItem(x, y);
                if (item != t.item) {
                    t.item = item;
                    recomputeHeld();
                    if (item >= 0) tick();
                    sendButtons();
                    invalidate();
                }
                break;
            }
            case ROLE_STICK: {
                float radius = stickRadiusDp * stickScale * density;
                float dx = x - t.originX, dy = y - t.originY;
                float length = (float) Math.hypot(dx, dy);
                float nx = 0, ny = 0;
                if (length > 0) {
                    float amount = Math.min(length / radius, 1);
                    amount = amount < DEADZONE ? 0 : (amount - DEADZONE) / (1 - DEADZONE);
                    nx = dx / length * amount;
                    ny = -dy / length * amount;   // the screen's y grows down, the stick's up
                }
                stickX = nx;
                stickY = ny;
                sendStick();
                invalidate();
                break;
            }
            case ROLE_POINTER:
                pointTo(x, y);
                break;
            default:
                break;
        }
    }

    private void up(int id) {
        Touch t = touches.get(id);
        if (t == null) return;
        touches.remove(id);
        if (t.role == ROLE_TOGGLE) {
            removeCallbacks(openEditor);
            toggleTouch = -1;
            tick();
            setUserHidden(!userHidden);
            ((GameActivity) getContext()).rememberOverlayHidden(userHidden);
        } else if (t.role == ROLE_BUTTON) { recomputeHeld(); sendButtons(); }
        else if (t.role == ROLE_STICK) { stickX = stickY = 0; sendStick(); }
        invalidate();
    }

    @Override
    public boolean onTouchEvent(MotionEvent e) {
        if (editing) return editTouch(e);
        switch (e.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN: {
                int index = e.getActionIndex();
                down(e.getPointerId(index), e.getX(index), e.getY(index));
                break;
            }
            case MotionEvent.ACTION_MOVE:
                for (int i = 0; i < e.getPointerCount(); i++) move(e.getPointerId(i), e.getX(i), e.getY(i));
                break;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP:
                up(e.getPointerId(e.getActionIndex()));
                break;
            case MotionEvent.ACTION_CANCEL:
                releaseAll();
                break;
            default:
                break;
        }
        return true;   // every touch is the game's, none goes on to the SDL surface
    }

    // ---- drawing ----

    private void drawItem(Canvas canvas, Item it, boolean pressed) {
        fill.setColor(pressed ? 0x66FFFFFF : 0x24FFFFFF);
        line.setColor(0x78FFFFFF);
        if (it.circle) {
            canvas.drawCircle(it.cx, it.cy, it.r * it.sc * density, fill);
            canvas.drawCircle(it.cx, it.cy, it.r * it.sc * density, line);
        } else {
            rect.set(it.cx - it.w / 2 * it.sc * density, it.cy - it.h / 2 * it.sc * density, it.cx + it.w / 2 * it.sc * density, it.cy + it.h / 2 * it.sc * density);
            float round = Math.min(it.w, it.h) / 4 * it.sc * density;
            canvas.drawRoundRect(rect, round, round, fill);
            canvas.drawRoundRect(rect, round, round, line);
        }
        text.setColor(0xB8FFFFFF);
        text.setTextSize(it.text * it.sc * density);
        canvas.drawText(it.label, it.cx, it.cy - (text.ascent() + text.descent()) / 2, text);
    }

    @Override
    protected void onDraw(Canvas canvas) {
        if (editing) canvas.drawColor(0x55000000);
        if (controlsShown()) {
            // The stick: where the finger started (or its resting place), and the knob.
            float radius = stickRadiusDp * stickScale * density;
            Touch stickTouch = null;
            for (int i = 0; i < touches.size(); i++) if (touches.valueAt(i).role == ROLE_STICK) stickTouch = touches.valueAt(i);
            float bx = stickTouch != null ? stickTouch.originX : stickRestPx, by = stickTouch != null ? stickTouch.originY : stickRestPy;
            fill.setColor(stickTouch != null ? 0x24FFFFFF : 0x10FFFFFF);
            canvas.drawCircle(bx, by, radius, fill);
            line.setColor(stickTouch != null ? 0x70FFFFFF : 0x40FFFFFF);
            canvas.drawCircle(bx, by, radius, line);
            fill.setColor(stickTouch != null ? 0x70FFFFFF : 0x30FFFFFF);
            canvas.drawCircle(bx + stickX * radius, by - stickY * radius, radius * 0.42f, fill);
            for (int i = 0; i < items.size(); i++) drawItem(canvas, items.get(i), (held & items.get(i).mask) != 0 && isHeldItem(i));
        }
        if (toggle != null && (!controllerPresent || editing)) {
            // The eye: an open one while the controls show, a faint one to bring them back.
            float r = toggle.r * toggle.sc * density;
            fill.setColor(0x30FFFFFF);
            canvas.drawCircle(toggle.cx, toggle.cy, r, fill);
            line.setColor(userHidden ? 0x60FFFFFF : 0xA0FFFFFF);
            canvas.drawCircle(toggle.cx, toggle.cy, r, line);
            rect.set(toggle.cx - r * 0.6f, toggle.cy - r * 0.32f, toggle.cx + r * 0.6f, toggle.cy + r * 0.32f);
            canvas.drawOval(rect, line);
            fill.setColor(userHidden ? 0x60FFFFFF : 0xA0FFFFFF);
            canvas.drawCircle(toggle.cx, toggle.cy, r * 0.16f, fill);
            if (userHidden) canvas.drawLine(toggle.cx - r * 0.6f, toggle.cy + r * 0.4f, toggle.cx + r * 0.6f, toggle.cy - r * 0.4f, line);
        }
        if (editing) drawEditor(canvas);
    }

    // ---- the player's layout: load, apply, save ----

    private static float[] entry(float x, float y, float scale) { return new float[] {x, y, scale}; }

    private void loadUser() {
        user.clear();
        if (userFile == null || !userFile.isFile()) return;
        try {
            byte[] bytes = java.nio.file.Files.readAllBytes(userFile.toPath());
            JSONObject controls = new JSONObject(new String(bytes, StandardCharsets.UTF_8)).optJSONObject("controls");
            if (controls == null) return;
            java.util.Iterator<String> keys = controls.keys();
            while (keys.hasNext()) {
                String id = keys.next();
                JSONObject c = controls.getJSONObject(id);
                float x = c.has("x") ? (float) c.getDouble("x") : Float.NaN, y = c.has("y") ? (float) c.getDouble("y") : Float.NaN;
                float s = c.has("scale") ? Math.max(MIN_SCALE, Math.min(MAX_SCALE, (float) c.getDouble("scale"))) : Float.NaN;
                if (Float.isNaN(y)) x = Float.NaN;
                if (!Float.isNaN(x)) { x = Math.max(0, Math.min(1, x)); y = Math.max(0, Math.min(1, y)); }
                user.put(id, entry(x, y, s));
            }
        } catch (IOException | JSONException e) {
            Log.e(TAG, "touch layout of the player (" + userFile + "): " + e + ", using the built-in one");
            user.clear();
        }
    }

    private void saveUser() {
        if (userFile == null) return;
        try {
            if (user.isEmpty()) { userFile.delete(); return; }
            JSONObject controls = new JSONObject();
            for (Map.Entry<String, float[]> en : user.entrySet()) {
                JSONObject c = new JSONObject();
                float[] u = en.getValue();
                if (!Float.isNaN(u[0])) { c.put("x", Math.round(u[0] * 10000) / 10000.0); c.put("y", Math.round(u[1] * 10000) / 10000.0); }
                if (!Float.isNaN(u[2])) c.put("scale", Math.round(u[2] * 1000) / 1000.0);
                controls.put(en.getKey(), c);
            }
            JSONObject root = new JSONObject();
            root.put("_comment", "The player's touch layout, on top of the built-in one. x and y are fractions of the window (the centre of the control), scale is a size factor.");
            root.put("version", 1);
            root.put("controls", controls);
            File parent = userFile.getParentFile();
            if (parent != null) parent.mkdirs();
            try (FileOutputStream out = new FileOutputStream(userFile)) { out.write(root.toString(1).getBytes(StandardCharsets.UTF_8)); }
        } catch (IOException | JSONException e) {
            Log.e(TAG, "can't save the touch layout to " + userFile + ": " + e);
        }
    }

    /** Copies the player's entries into the controls (position fractions and size factors); the built-in layout stays underneath. */
    private void applyUser() {
        float[] s = user.get("stick");
        stickScale = s == null || Float.isNaN(s[2]) ? 1 : s[2];
        stickFixed = s != null && !Float.isNaN(s[0]);
        if (stickFixed) { stickFx = s[0]; stickFy = s[1]; }
        for (Item it : items) applyUser(it);
        if (toggle != null) applyUser(toggle);
    }

    private void applyUser(Item it) {
        float[] u = it.id == null ? null : user.get(it.id);
        it.sc = u == null || Float.isNaN(u[2]) ? 1 : u[2];
        it.fixed = u != null && !Float.isNaN(u[0]);
        if (it.fixed) { it.fx = u[0]; it.fy = u[1]; }
    }

    private void relayout() {
        applyUser();
        placeAll();
        invalidate();
    }

    // ---- the layout editor ----

    private boolean editing;
    private String selected;
    private float grabX, grabY, savedAlpha = 1;
    private int dragId = -1, panelPointer = -1, pinchId = -1, toggleTouch = -1;
    private float pinchStart, pinchBase;
    private boolean panelTop;
    private final RectF panel = new RectF(), slider = new RectF(), buttonReset = new RectF(), buttonDone = new RectF(), bounds = new RectF();
    private final Runnable openEditor = () -> {
        Touch t = touches.get(toggleTouch);
        if (t == null || t.role != ROLE_TOGGLE) return;
        touches.remove(toggleTouch);
        toggleTouch = -1;
        performHapticFeedback(HapticFeedbackConstants.LONG_PRESS, HapticFeedbackConstants.FLAG_IGNORE_GLOBAL_SETTING);
        startEditing();
    };

    boolean isEditing() { return editing; }

    /** Opens the editor: the game stops getting touches from the controls (and from the pointer) until Done. */
    void startEditing() {
        if (editing) return;
        releaseAll();
        editing = true;
        selected = null;
        dragId = panelPointer = pinchId = -1;
        savedAlpha = getAlpha();
        setAlpha(1f);
        invalidate();
    }

    void finishEditing() {
        if (!editing) return;
        editing = false;
        selected = null;
        dragId = panelPointer = pinchId = -1;
        saveUser();
        setAlpha(savedAlpha);
        releaseAll();
    }

    private void resetUser() {
        user.clear();
        selected = null;
        saveUser();
        relayout();
    }

    private static String nameOf(String id) {
        switch (id) {
            case "stick": return "Stick";
            case "dpad": return "D-pad";
            case "toggle": return "Eye button";
            case "minus": return "Button -";
            case "plus": return "Button +";
            case "one": return "Button 1";
            case "two": return "Button 2";
            default: return "Button " + id.toUpperCase(java.util.Locale.ROOT);
        }
    }

    private Item itemById(String id) {
        if (id.equals("toggle")) return toggle;
        for (Item it : items) if (id.equals(it.id)) return it;
        return null;
    }

    /** The centre of a control, in pixels. */
    private float[] centreOf(String id) {
        if (id.equals("stick")) return new float[] {stickRestPx, stickRestPy};
        if (id.equals("dpad")) {
            float sx = 0, sy = 0; int n = 0;
            for (Item it : items) if (it.arm) { sx += it.cx; sy += it.cy; n++; }
            return n == 0 ? new float[] {0, 0} : new float[] {sx / n, sy / n};
        }
        Item it = itemById(id);
        return it == null ? new float[] {0, 0} : new float[] {it.cx, it.cy};
    }

    private float scaleOf(String id) {
        float[] u = user.get(id);
        return u == null || Float.isNaN(u[2]) ? 1 : u[2];
    }

    private void setScale(String id, float scale) {
        scale = Math.max(MIN_SCALE, Math.min(MAX_SCALE, scale));
        float[] u = user.get(id);
        if (u == null) { u = entry(Float.NaN, Float.NaN, scale); user.put(id, u); }
        u[2] = scale;
        relayout();
    }

    private void setCentre(String id, float x, float y) {
        int w = getWidth(), h = getHeight();
        if (w == 0 || h == 0) return;
        x = Math.max(safeLeft, Math.min(x, w - safeRight));
        y = Math.max(0, Math.min(y, h));
        float[] u = user.get(id);
        if (u == null) { u = entry(x / w, y / h, Float.NaN); user.put(id, u); }
        u[0] = x / w;
        u[1] = y / h;
        relayout();
    }

    /** The control's outline on the screen, in pixels. */
    private RectF boundsOf(String id, RectF out) {
        float[] c = centreOf(id);
        if (id.equals("stick")) {
            float r = stickRadiusDp * stickScale * density;
            out.set(c[0] - r, c[1] - r, c[0] + r, c[1] + r);
        } else if (id.equals("dpad")) {
            out.set(Float.MAX_VALUE, Float.MAX_VALUE, -Float.MAX_VALUE, -Float.MAX_VALUE);
            for (Item it : items) if (it.arm) {
                out.left = Math.min(out.left, it.cx - it.w * it.sc * density / 2);
                out.top = Math.min(out.top, it.cy - it.h * it.sc * density / 2);
                out.right = Math.max(out.right, it.cx + it.w * it.sc * density / 2);
                out.bottom = Math.max(out.bottom, it.cy + it.h * it.sc * density / 2);
            }
        } else {
            Item it = itemById(id);
            if (it == null) { out.setEmpty(); return out; }
            float hw = (it.circle ? it.r : it.w / 2) * it.sc * density, hh = (it.circle ? it.r : it.h / 2) * it.sc * density;
            out.set(c[0] - hw, c[1] - hh, c[0] + hw, c[1] + hh);
        }
        return out;
    }

    /** Which control is under a finger, or null. The nearest centre wins among those that are hit. */
    private String editHit(float x, float y) {
        float slop = 6 * density;
        String best = null;
        float bestDistance = Float.MAX_VALUE;
        List<String> ids = new ArrayList<>();
        ids.add("stick");
        ids.add("dpad");
        if (toggle != null) ids.add("toggle");
        for (Item it : items) if (!it.arm && it.id != null && !it.id.isEmpty()) ids.add(it.id);
        for (String id : ids) {
            boundsOf(id, bounds);
            boolean hit;
            if (id.equals("stick")) {
                float[] c = centreOf(id);
                float r = stickRadiusDp * stickScale * density + slop;
                hit = (x - c[0]) * (x - c[0]) + (y - c[1]) * (y - c[1]) <= r * r;
            } else {
                hit = x >= bounds.left - slop && x <= bounds.right + slop && y >= bounds.top - slop && y <= bounds.bottom + slop;
                Item it = itemById(id);
                if (hit && it != null && it.circle) hit = it.hit(x, y, slop, density);
            }
            if (!hit) continue;
            float[] c = centreOf(id);
            float d = (x - c[0]) * (x - c[0]) + (y - c[1]) * (y - c[1]);
            if (d < bestDistance) { best = id; bestDistance = d; }
        }
        return best;
    }

    private void layoutPanel() {
        float w = getWidth(), h = getHeight();
        float pw = Math.min(w - safeLeft - safeRight - 16 * density, 380 * density), ph = 136 * density;
        float left = safeLeft + (w - safeLeft - safeRight - pw) / 2, top = panelTop ? Math.max(safeTop, 8 * density) : h - ph - 8 * density;
        panel.set(left, top, left + pw, top + ph);
        float pad = 14 * density;
        float row = ph / 3;
        slider.set(left + pad + 70 * density, top + row * 1.5f - 14 * density, left + pw - pad - 46 * density, top + row * 1.5f + 14 * density);
        float bh = 34 * density;
        buttonReset.set(left + pad, top + ph - pad - bh, left + pw / 2 - pad / 2, top + ph - pad);
        buttonDone.set(left + pw / 2 + pad / 2, top + ph - pad - bh, left + pw - pad, top + ph - pad);
    }

    /** The panel sits at the bottom, and goes to the top while the chosen control would be under it. */
    private void dockPanel() {
        panelTop = false;
        layoutPanel();
        if (selected != null) {
            boundsOf(selected, bounds);
            if (RectF.intersects(panel, bounds)) panelTop = true;
        }
        layoutPanel();
    }

    private void setSliderFrom(float x) {
        if (selected == null) return;
        float f = Math.max(0, Math.min(1, (x - slider.left) / slider.width()));
        setScale(selected, MIN_SCALE + f * (MAX_SCALE - MIN_SCALE));
    }

    private boolean editTouch(MotionEvent e) {
        int action = e.getActionMasked();
        switch (action) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN: {
                int index = e.getActionIndex(), id = e.getPointerId(index);
                float x = e.getX(index), y = e.getY(index);
                layoutPanel();
                if (panel.contains(x, y)) {
                    if (buttonDone.contains(x, y)) finishEditing();
                    else if (buttonReset.contains(x, y)) { resetUser(); dockPanel(); }
                    else if (selected != null && x >= slider.left - 20 * density && x <= slider.right + 20 * density
                            && y >= slider.top - 12 * density && y <= slider.bottom + 12 * density) { panelPointer = id; setSliderFrom(x); }
                    break;
                }
                if (dragId >= 0 && selected != null && pinchId < 0) {
                    // A second finger on the screen while one holds a control: pinch to resize it.
                    int first = e.findPointerIndex(dragId);
                    if (first >= 0) {
                        pinchId = id;
                        pinchStart = Math.max(1, (float) Math.hypot(e.getX(first) - x, e.getY(first) - y));
                        pinchBase = scaleOf(selected);
                    }
                    break;
                }
                String hit = editHit(x, y);
                selected = hit;
                if (hit != null) {
                    float[] c = centreOf(hit);
                    grabX = x - c[0];
                    grabY = y - c[1];
                    dragId = id;
                    tick();
                }
                dockPanel();
                invalidate();
                break;
            }
            case MotionEvent.ACTION_MOVE:
                for (int i = 0; i < e.getPointerCount(); i++) {
                    int id = e.getPointerId(i);
                    if (id == panelPointer) setSliderFrom(e.getX(i));
                    else if (id == dragId && selected != null && pinchId < 0) setCentre(selected, e.getX(i) - grabX, e.getY(i) - grabY);
                }
                if (pinchId >= 0 && dragId >= 0 && selected != null) {
                    int a = e.findPointerIndex(dragId), b = e.findPointerIndex(pinchId);
                    if (a >= 0 && b >= 0) setScale(selected, pinchBase * (float) Math.hypot(e.getX(a) - e.getX(b), e.getY(a) - e.getY(b)) / pinchStart);
                }
                break;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP: {
                int id = e.getPointerId(e.getActionIndex());
                if (id == panelPointer) { panelPointer = -1; saveUser(); }
                if (id == pinchId) { pinchId = -1; saveUser(); }
                if (id == dragId) { dragId = -1; pinchId = -1; saveUser(); dockPanel(); invalidate(); }
                break;
            }
            case MotionEvent.ACTION_CANCEL:
                dragId = panelPointer = pinchId = -1;
                saveUser();
                break;
            default:
                break;
        }
        return true;   // while editing, nothing reaches the game
    }

    private void drawEditor(Canvas canvas) {
        if (selected != null) {
            boundsOf(selected, bounds);
            float pad = 6 * density;
            bounds.inset(-pad, -pad);
            line.setColor(0xFFFFD36A);
            canvas.drawRoundRect(bounds, 10 * density, 10 * density, line);
        }
        layoutPanel();
        fill.setColor(0xE6202838);
        canvas.drawRoundRect(panel, 12 * density, 12 * density, fill);
        line.setColor(0xFFFFD36A);
        canvas.drawRoundRect(panel, 12 * density, 12 * density, line);
        float row = panel.height() / 3, pad = 14 * density;
        text.setColor(0xFFFFFFFF);
        text.setTextAlign(Paint.Align.LEFT);
        text.setTextSize(13 * density);
        float ty = panel.top + row * 0.5f - (text.ascent() + text.descent()) / 2;
        canvas.drawText(selected == null ? "Layout editor: drag a control to move it" : nameOf(selected) + ": drag to move, pinch or use the slider to resize", panel.left + pad, ty, text);
        // The size slider, dim when nothing is chosen.
        int alpha = selected == null ? 0x55 : 0xFF;
        text.setTextSize(12 * density);
        text.setColor((alpha << 24) | 0xFFFFFF);
        float sy = slider.centerY() - (text.ascent() + text.descent()) / 2;
        canvas.drawText("Size", panel.left + pad, sy, text);
        float scale = selected == null ? 1 : scaleOf(selected);
        text.setTextAlign(Paint.Align.RIGHT);
        canvas.drawText(Math.round(scale * 100) + "%", panel.right - pad, sy, text);
        text.setTextAlign(Paint.Align.CENTER);
        fill.setColor((alpha << 24) | 0x808890);
        canvas.drawRoundRect(slider.left, slider.centerY() - 2 * density, slider.right, slider.centerY() + 2 * density, 2 * density, 2 * density, fill);
        float f = (scale - MIN_SCALE) / (MAX_SCALE - MIN_SCALE);
        fill.setColor((alpha << 24) | 0xFFD36A);
        canvas.drawCircle(slider.left + f * slider.width(), slider.centerY(), 9 * density, fill);
        for (int k = 0; k < 2; k++) {
            RectF b = k == 0 ? buttonReset : buttonDone;
            fill.setColor(k == 0 ? 0xFF5A6478 : 0xFF3F9E5A);
            canvas.drawRoundRect(b, 8 * density, 8 * density, fill);
            text.setColor(0xFFFFFFFF);
            text.setTextSize(13 * density);
            canvas.drawText(k == 0 ? "Reset to default" : "Done", b.centerX(), b.centerY() - (text.ascent() + text.descent()) / 2, text);
        }
    }

    private boolean isHeldItem(int index) {
        for (int i = 0; i < touches.size(); i++) {
            Touch t = touches.valueAt(i);
            if (t.role == ROLE_BUTTON && t.item == index) return true;
        }
        return false;
    }
}
