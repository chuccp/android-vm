package android.graphics;

public class Color {
    public static final int BLACK = 0xff000000;
    public static final int DKGRAY = 0xff444444;
    public static final int GRAY = 0xff888888;
    public static final int LTGRAY = 0xffcccccc;
    public static final int WHITE = 0xffffffff;
    public static final int RED = 0xffff0000;
    public static final int GREEN = 0xff00ff00;
    public static final int BLUE = 0xff0000ff;
    public static final int YELLOW = 0xffffff00;
    public static final int CYAN = 0xff00ffff;
    public static final int MAGENTA = 0xffff00ff;
    public static final int TRANSPARENT = 0;

    public static int alpha(int color) { return color >>> 24; }
    public static int red(int color) { return (color >> 16) & 0xff; }
    public static int green(int color) { return (color >> 8) & 0xff; }
    public static int blue(int color) { return color & 0xff; }

    public static int rgb(int red, int green, int blue) {
        return 0xff000000 | (red << 16) | (green << 8) | blue;
    }

    public static int argb(int alpha, int red, int green, int blue) {
        return (alpha << 24) | (red << 16) | (green << 8) | blue;
    }

    public static int parseColor(String colorString) {
        if (colorString.charAt(0) == '#') {
            long color = Long.parseLong(colorString.substring(1), 16);
            if (colorString.length() == 7) color |= 0x00000000ff000000L;
            else if (colorString.length() != 9)
                throw new IllegalArgumentException("Unknown color");
            return (int) color;
        }
        if ("red".equals(colorString)) return RED;
        if ("green".equals(colorString)) return GREEN;
        if ("blue".equals(colorString)) return BLUE;
        if ("black".equals(colorString)) return BLACK;
        if ("white".equals(colorString)) return WHITE;
        if ("gray".equals(colorString)) return GRAY;
        if ("cyan".equals(colorString)) return CYAN;
        if ("magenta".equals(colorString)) return MAGENTA;
        if ("yellow".equals(colorString)) return YELLOW;
        throw new IllegalArgumentException("Unknown color");
    }

    public static void colorToHSV(int color, float[] hsv) {
        int r = red(color);
        int g = green(color);
        int b = blue(color);
        int max = Math.max(r, Math.max(g, b));
        int min = Math.min(r, Math.min(g, b));
        int delta = max - min;
        float hue;
        if (delta == 0) hue = 0f;
        else if (max == r) hue = 60f * (((g - b) / (float) delta) % 6f);
        else if (max == g) hue = 60f * (((b - r) / (float) delta) + 2f);
        else hue = 60f * (((r - g) / (float) delta) + 4f);
        if (hue < 0f) hue += 360f;
        hsv[0] = hue;
        hsv[1] = max == 0 ? 0f : delta / (float) max;
        hsv[2] = max / 255f;
    }
}
