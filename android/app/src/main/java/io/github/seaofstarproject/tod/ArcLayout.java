package io.github.seaofstarproject.tod;

import java.util.List;

/**
 * Places the touch face buttons in an arc around the main one (A), measured from the game's picture rather than from
 * fixed pixels, so any screen shape works. Pure geometry with no Android types, so it can be run off the device.
 * All values are pixels; angles are degrees counter-clockwise from "right" with up on the screen as 90.
 */
final class ArcLayout {
    private static final float[][] NONE = new float[0][];

    private ArcLayout() { }

    /** A disc to place. A fixed one (the origin) is never moved; the others get x and y from their preferred angle and distance. */
    static final class Disc {
        float x, y, r;
        float angle, dist;     // preferred position relative to the origin (dist is the centre to centre distance)
        Disc(float r, float angle, float dist) { this.r = r; this.angle = angle; this.dist = dist; }
    }

    /** The rectangle the game's picture covers inside a window: the whole window when stretched, else the largest one of that aspect, centred. */
    static float[] picture(float width, float height, float aspect, boolean stretch) {
        if (stretch || aspect <= 0) return new float[] {0, 0, width, height};
        float pw = Math.min(width, height * aspect), ph = pw / aspect;
        float left = (width - pw) / 2, top = (height - ph) / 2;
        return new float[] {left, top, left + pw, top + ph};
    }

    /** A picture-relative fraction rectangle {l, t, r, b} turned into pixels inside the picture. */
    static float[] inPicture(float[] picture, float[] fraction) {
        float pw = picture[2] - picture[0], ph = picture[3] - picture[1];
        return new float[] {picture[0] + fraction[0] * pw, picture[1] + fraction[1] * ph, picture[0] + fraction[2] * pw, picture[1] + fraction[3] * ph};
    }

    static boolean hitsRect(float x, float y, float r, float[] rect) {
        float nx = Math.max(rect[0], Math.min(x, rect[2])), ny = Math.max(rect[1], Math.min(y, rect[3]));
        float dx = x - nx, dy = y - ny;
        return dx * dx + dy * dy < r * r;
    }

    /**
     * Puts each disc of {@code arcs} (in order) at its preferred angle and distance from {@code origin}. A spot is taken only when
     * the disc keeps {@code gap} from the avoided rectangles, from the origin and from the discs placed before it, and stays inside
     * {@code bounds}; if not, the angle is turned toward straight up and then past it, and the distance grows, until one fits.
     * Then the same without the avoided rectangles; the last resort is the preferred spot clamped into the bounds.
     */
    static void arrange(Disc origin, List<Disc> arcs, float[][] avoid, float[] bounds, float gap, float growStep) {
        int placed = 0;
        for (Disc d : arcs) {
            boolean found = false;
            // Keeping off the HUD matters less than not stacking buttons: if nothing fits, try again without the avoided rectangles.
            for (int pass = 0; pass < 2 && !found; pass++)
            for (int grow = 0; grow < 8 && !found; grow++) {
                float dist = d.dist + grow * growStep;
                // From the preferred angle toward up (90), then past it, one degree at a time.
                for (int step = 0; step <= 150 && !found; step++) {
                    float toward = d.angle > 90 ? -1 : 1;
                    float angle = d.angle + toward * step;
                    if (angle < -10 || angle > 190) break;
                    float x = origin.x + (float) Math.cos(Math.toRadians(angle)) * dist;
                    float y = origin.y - (float) Math.sin(Math.toRadians(angle)) * dist;
                    if (fits(x, y, d.r, origin, arcs, placed, pass == 0 ? avoid : NONE, bounds, gap)) { d.x = x; d.y = y; found = true; }
                }
            }
            if (!found) {
                float x = origin.x + (float) Math.cos(Math.toRadians(d.angle)) * d.dist, y = origin.y - (float) Math.sin(Math.toRadians(d.angle)) * d.dist;
                d.x = Math.max(bounds[0] + d.r, Math.min(x, bounds[2] - d.r));
                d.y = Math.max(bounds[1] + d.r, Math.min(y, bounds[3] - d.r));
            }
            placed++;
        }
    }

    private static boolean fits(float x, float y, float r, Disc origin, List<Disc> arcs, int placed, float[][] avoid, float[] bounds, float gap) {
        if (x - r < bounds[0] || y - r < bounds[1] || x + r > bounds[2] || y + r > bounds[3]) return false;
        for (float[] rect : avoid) if (hitsRect(x, y, r + gap, rect)) return false;
        if (Math.hypot(x - origin.x, y - origin.y) < r + origin.r + gap) return false;
        for (int i = 0; i < placed; i++) {
            Disc o = arcs.get(i);
            if (Math.hypot(x - o.x, y - o.y) < r + o.r + gap) return false;
        }
        return true;
    }
}
