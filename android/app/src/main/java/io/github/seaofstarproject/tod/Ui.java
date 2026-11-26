package io.github.seaofstarproject.tod;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.ColorFilter;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.PixelFormat;
import android.graphics.RectF;
import android.graphics.Shader;
import android.graphics.Typeface;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.LayerDrawable;
import android.graphics.drawable.StateListDrawable;
import android.view.Gravity;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

/**
 * The launcher's look, taken from the Windows launcher's Theme (launcher/src/main.cpp): a sky with hills behind, parchment cards with a
 * thick brown outline, gold and green "candy" buttons. Plain views and drawables only; every colour below is that Theme's value.
 */
final class Ui {
    private Ui() { }

    // Sky, sea, clouds, hills.
    static final int SKY_TOP = 0xFF60BEF0, SKY_BOTTOM = 0xFFBAE6F8, SUN = 0x96FFF4B0, SEA = 0xFF42A0D2;
    static final int CLOUD = 0xD7FFFFFF, CLOUD_SHADE = 0xD7CEE8F8, HILL_BACK = 0xFF5CB654, HILL_FRONT = 0xFF7CCC60, HILL_LINE = 0xFF3C8038;
    // Text on the sky, titles.
    static final int BG_TEXT = 0xFF08223E, BRAND_TEXT = 0xFF7A280E, TITLE_OUTLINE = 0xFF122E52;
    // Panels and the text on them.
    static final int PANEL_TOP = 0xFFFDF3D8, PANEL_BOTTOM = 0xFFF4E2BA, PANEL_OUTLINE = 0xFF4E2E16, PANEL_EDGE = 0xFFD6B074;
    static final int CARD_TEXT = 0xFF3E2410, CARD_DIM = 0xFF644224, CARD_MUTED = 0xFF7E5C3A, CARD_TITLE = 0xFF8C541E, ACCENT = 0xFFB02818;
    static final int GOOD = 0xFF16702C, WARN = 0xFF925600, BAD = 0xFFB82222;
    static final int SLOT_FILL = 0xFFFFFAEC, ROW_BORDER = 0xFFC8AA78, ROW_DIVIDER = 0x46785430;
    static final int PILL_OK = 0xFF7EE05C, PILL_BAD = 0xFFFF8470, PILL_TEXT = 0xFF321E0E;
    // The log is a dark console, as on the Windows Build page.
    static final int CONSOLE = 0xFF2E221C, CONSOLE_BORDER = 0xFF1A100A, LOG_TEXT = 0xFFECE2CC, LOG_HEADER = 0xFFFFC864;

    /** One button colour set: face top, face bottom, lip (the thick lower edge), outline, label. */
    static final class Candy {
        final int top, bottom, lip, outline, label;
        Candy(int top, int bottom, int lip, int outline, int label) { this.top = top; this.bottom = bottom; this.lip = lip; this.outline = outline; this.label = label; }
    }
    static final Candy GREEN = new Candy(0xFF88E254, 0xFF3EB02C, 0xFF22761E, 0xFF164218, 0xFFFFFFFF);
    static final Candy GOLD = new Candy(0xFFFFE86E, 0xFFF4B624, 0xFFBA7A0C, 0xFF603C0A, 0xFF543006);
    static final Candy RED = new Candy(0xFFFF8268, 0xFFD63A2C, 0xFF8C1C16, 0xFF50100C, 0xFFFFF8F4);

    static int dp(Context context, float value) { return Math.round(value * context.getResources().getDisplayMetrics().density); }

    static GradientDrawable round(Context context, int top, int bottom, int stroke, float strokeDp, float radiusDp) {
        GradientDrawable d = new GradientDrawable(GradientDrawable.Orientation.TOP_BOTTOM, new int[] {top, bottom});
        d.setCornerRadius(dp(context, radiusDp));
        if (strokeDp > 0) d.setStroke(dp(context, strokeDp), stroke);
        return d;
    }

    private static Drawable candyFace(Context context, Candy c, boolean pressed, float radiusDp) {
        GradientDrawable lip = round(context, c.lip, c.lip, c.outline, 2, radiusDp);
        GradientDrawable face = round(context, pressed ? c.bottom : c.top, pressed ? c.lip : c.bottom, c.outline, 2, radiusDp);
        LayerDrawable layers = new LayerDrawable(new Drawable[] {lip, face});
        layers.setLayerInset(1, 0, pressed ? dp(context, 3) : 0, 0, pressed ? dp(context, 1) : dp(context, 4));
        return layers;
    }

    /** A chunky button: a face over a darker lip that sinks when pressed, a disabled one greyed. */
    static Button button(Context context, String text, Candy candy, boolean primary) {
        Button b = new Button(context);
        float radius = primary ? 14 : 11;
        StateListDrawable states = new StateListDrawable();
        states.addState(new int[] {-android.R.attr.state_enabled}, candyFace(context, new Candy(0xFFC8C8C4, 0xFFA4A4A0, 0xFF787874, 0xFF525250, 0xFF282826), false, radius));
        states.addState(new int[] {android.R.attr.state_pressed}, candyFace(context, candy, true, radius));
        states.addState(new int[] {}, candyFace(context, candy, false, radius));
        b.setBackground(states);
        b.setStateListAnimator(null);
        b.setText(text);
        b.setAllCaps(false);
        b.setTypeface(Typeface.DEFAULT_BOLD);
        b.setTextSize(primary ? 22 : 14);
        b.setTextColor(candy.label);
        if (candy.label == 0xFFFFFFFF) b.setShadowLayer(dp(context, 1.5f), 0, dp(context, 1.5f), candy.outline);
        b.setMinHeight(0);
        b.setMinimumHeight(dp(context, primary ? 64 : 40));
        b.setMinWidth(0);
        b.setMinimumWidth(dp(context, primary ? 160 : 0));
        b.setPadding(dp(context, primary ? 28 : 18), dp(context, 6), dp(context, primary ? 28 : 18), dp(context, 10));
        return b;
    }

    /** A parchment card. */
    static LinearLayout card(Context context) {
        LinearLayout card = new LinearLayout(context);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setBackground(round(context, PANEL_TOP, PANEL_BOTTOM, PANEL_OUTLINE, 2.5f, 16));
        int p = dp(context, 14);
        card.setPadding(p, p, p, p);
        return card;
    }

    /** A recessed row inside a card. */
    static LinearLayout slot(Context context) {
        LinearLayout slot = new LinearLayout(context);
        slot.setOrientation(LinearLayout.VERTICAL);
        slot.setBackground(round(context, SLOT_FILL, SLOT_FILL, ROW_BORDER, 1.5f, 10));
        int p = dp(context, 10);
        slot.setPadding(p, p - dp(context, 2), p, p - dp(context, 2));
        return slot;
    }

    /** The small gold tab that names a card ("DISPLAY"). */
    static TextView tab(Context context, String text) {
        TextView t = new TextView(context);
        t.setText(text);
        t.setTextSize(11);
        t.setTypeface(Typeface.DEFAULT_BOLD);
        t.setTextColor(0xFF543006);
        t.setGravity(Gravity.CENTER);
        t.setBackground(candyFace(context, GOLD, false, 9));
        t.setPadding(dp(context, 14), dp(context, 3), dp(context, 14), dp(context, 7));
        return t;
    }

    /** The coloured status pill in the header. */
    static TextView pill(Context context, String text, int fill) {
        TextView t = new TextView(context);
        t.setText(text);
        t.setTextSize(12);
        t.setTypeface(Typeface.DEFAULT_BOLD);
        t.setTextColor(PILL_TEXT);
        t.setBackground(round(context, fill, fill, 0xFF2E7A1C, 2, 14));
        t.setPadding(dp(context, 14), dp(context, 5), dp(context, 14), dp(context, 5));
        return t;
    }

    static LinearLayout.LayoutParams lp(int w, int h) { return new LinearLayout.LayoutParams(w, h); }

    static LinearLayout.LayoutParams weighted(float weight) { return new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, weight); }

    /** Heavy white letters with a dark outline, as the Windows launcher's titles. */
    static final class OutlinedText extends TextView {
        private final int outline, strokePx;
        OutlinedText(Context context, int outline, int strokePx) {
            super(context);
            this.outline = outline;
            this.strokePx = strokePx;
            setTypeface(Typeface.DEFAULT_BOLD);
            setTextColor(Color.WHITE);
            setPadding(strokePx, strokePx, strokePx, strokePx * 2);
        }
        @Override protected void onDraw(Canvas canvas) {
            if (getLayout() == null) return;
            // The layout is drawn twice with the view's own paint: the outline (a stroke), then the letters. Not super.onDraw, which
            // resets the colour from the text colour, and setTextColor would invalidate the view again and again.
            Paint paint = getPaint();
            canvas.save();
            canvas.translate(getCompoundPaddingLeft(), getExtendedPaddingTop());
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeJoin(Paint.Join.ROUND);
            paint.setStrokeWidth(strokePx * 2);
            paint.setColor(outline);
            getLayout().draw(canvas);
            paint.setStyle(Paint.Style.FILL);
            paint.setColor(Color.WHITE);
            getLayout().draw(canvas);
            canvas.restore();
        }
    }

    /** The window background: sky, sun, sea, clouds and two rows of hills at the bottom. */
    static final class Scenery extends Drawable {
        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Path path = new Path();
        private final RectF oval = new RectF();
        private final float density;
        Scenery(Context context) { density = context.getResources().getDisplayMetrics().density; }

        private void cloud(Canvas c, float x, float y, float s) {
            paint.setColor(CLOUD_SHADE);
            oval.set(x, y + 10 * s, x + 150 * s, y + 38 * s);
            c.drawOval(oval, paint);
            paint.setColor(CLOUD);
            oval.set(x, y + 6 * s, x + 150 * s, y + 32 * s);
            c.drawOval(oval, paint);
            oval.set(x + 28 * s, y - 14 * s, x + 96 * s, y + 24 * s);
            c.drawOval(oval, paint);
            oval.set(x + 70 * s, y - 4 * s, x + 126 * s, y + 24 * s);
            c.drawOval(oval, paint);
        }

        private void hills(Canvas c, float w, float h, float base, float amp, float length, float phase, int fill) {
            path.reset();
            path.moveTo(0, h);
            for (float x = 0; x <= w + 8; x += 8) path.lineTo(x, base + (float) Math.sin(x / length * 6.2832 + phase) * amp);
            path.lineTo(w + 8, h);
            path.close();
            paint.setStyle(Paint.Style.FILL);
            paint.setColor(fill);
            c.drawPath(path, paint);
        }

        @Override public void draw(Canvas canvas) {
            float w = getBounds().width(), h = getBounds().height();
            paint.setShader(new LinearGradient(0, 0, 0, h, SKY_TOP, SKY_BOTTOM, Shader.TileMode.CLAMP));
            canvas.drawRect(0, 0, w, h, paint);
            paint.setShader(null);
            paint.setColor(SUN);
            canvas.drawCircle(w * 0.93f, h * 0.06f, 90 * density, paint);
            paint.setColor(SEA);
            canvas.drawRect(0, h * 0.74f, w, h, paint);
            cloud(canvas, w * 0.42f, h * 0.10f, density * 1.1f);
            cloud(canvas, w * 0.74f, h * 0.30f, density * 0.9f);
            hills(canvas, w, h, h * 0.86f, 14 * density, w * 0.9f, 0.6f, HILL_BACK);
            hills(canvas, w, h, h * 0.92f, 12 * density, w * 0.7f, 3.4f, HILL_FRONT);
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(2 * density);
            paint.setColor(HILL_LINE);
            path.rewind();
            for (float x = 0; x <= w + 8; x += 8) {
                float y = h * 0.92f + (float) Math.sin(x / (w * 0.7f) * 6.2832 + 3.4f) * 12 * density;
                if (x == 0) path.moveTo(x, y); else path.lineTo(x, y);
            }
            canvas.drawPath(path, paint);
            paint.setStyle(Paint.Style.FILL);
        }
        @Override public void setAlpha(int alpha) { }
        @Override public void setColorFilter(ColorFilter filter) { }
        @Override public int getOpacity() { return PixelFormat.OPAQUE; }
    }
}
