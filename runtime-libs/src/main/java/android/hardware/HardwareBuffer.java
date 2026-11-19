package android.hardware;

/**
 * 硬件缓冲（AOSP android.hardware.HardwareBuffer；§31.7 C 档 lib 骨架）。
 * 仅 Bitmap.getHardwareBuffer 引用面载体。
 */
public final class HardwareBuffer {
    HardwareBuffer() {
    }

    public void close() {
    }

    public boolean isClosed() {
        return true;
    }
}
