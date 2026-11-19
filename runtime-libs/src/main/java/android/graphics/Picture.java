package android.graphics;

/**
 * 录制画布（AOSP android.graphics.Picture；§31.7 C 档 lib 骨架）。
 */
public class Picture {
    public Picture() {
    }

    public Canvas beginRecording(int width, int height) {
        return new Canvas();
    }

    public void endRecording() {
    }

    public int getWidth() {
        return 0;
    }

    public int getHeight() {
        return 0;
    }

    public void draw(Canvas canvas) {
    }
}
