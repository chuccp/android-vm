package java.util.zip;

import java.nio.ByteBuffer;

public class Deflater {
    public static final int DEFAULT_COMPRESSION = -1;
    public static final int DEFLATED = 8;
    public static final int NO_COMPRESSION = 0;
    public static final int BEST_SPEED = 1;
    public static final int BEST_COMPRESSION = 9;
    public static final int DEFAULT_STRATEGY = 0;
    public static final int FILTERED = 1;
    public static final int HUFFMAN_ONLY = 2;
    public static final int NO_FLUSH = 0;
    public static final int SYNC_FLUSH = 2;
    public static final int FULL_FLUSH = 3;

    private long handle;
    private byte[] input;
    private int inputOffset;
    private int inputLength;
    private boolean finishing;
    private int level;
    private int strategy = DEFAULT_STRATEGY;
    private boolean paramsChanged;
    private long bytesRead;
    private long bytesWritten;

    public Deflater(int level, boolean nowrap) {
        this.level = level;
        handle = init(level, strategy, nowrap);
    }

    public Deflater(int level) {
        this(level, false);
    }

    public Deflater() {
        this(DEFAULT_COMPRESSION, false);
    }

    public void setInput(byte[] input) {
        setInput(input, 0, input.length);
    }

    public void setInput(byte[] input, int offset, int length) {
        if (offset < 0 || length < 0 ||
                offset > input.length - length) {
            throw new ArrayIndexOutOfBoundsException();
        }
        this.input = input;
        inputOffset = offset;
        inputLength = length;
    }

    public void setInput(ByteBuffer input) {
        if (input == null) throw new NullPointerException();
        int length = input.remaining();
        byte[] bytes = new byte[length];
        input.get(bytes);
        setInput(bytes);
    }

    public void setLevel(int level) {
        if ((level < 0 || level > 9) && level != DEFAULT_COMPRESSION) {
            throw new IllegalArgumentException();
        }
        this.level = level;
        paramsChanged = true;
    }

    public void setStrategy(int strategy) {
        if (strategy < 0 || strategy > 2) {
            throw new IllegalArgumentException();
        }
        this.strategy = strategy;
        paramsChanged = true;
    }

    public void setDictionary(byte[] dictionary) {
        setDictionary(dictionary, 0, dictionary.length);
    }

    public void setDictionary(
            byte[] dictionary, int offset, int length) {
        nativeSetDictionary(handle, dictionary, offset, length);
    }

    public boolean needsInput() {
        return inputLength == 0;
    }

    public void finish() {
        finishing = true;
    }

    public boolean finished() {
        return nativeFinished(handle);
    }

    public int deflate(byte[] output) {
        return deflate(output, 0, output.length, NO_FLUSH);
    }

    public int deflate(byte[] output, int offset, int length) {
        return deflate(output, offset, length, NO_FLUSH);
    }

    public int deflate(byte[] output, int offset, int length, int flush) {
        if (offset < 0 || length < 0 ||
                offset > output.length - length) {
            throw new ArrayIndexOutOfBoundsException();
        }
        int params = paramsChanged
                ? 1 | strategy << 1 | level << 3
                : 0;
        int written = nativeDeflate(
                handle,
                input, inputOffset, inputLength,
                output, offset, length,
                finishing ? 4 : flush,
                params);
        if (written < 0) throw new IllegalStateException("Deflater failed");
        if (params != 0) paramsChanged = false;
        int consumed = inputLength - nativeRemaining(handle);
        inputOffset += consumed;
        inputLength = nativeRemaining(handle);
        bytesRead += consumed;
        bytesWritten += written;
        return written;
    }

    public int deflate(ByteBuffer output) {
        return deflate(output, NO_FLUSH);
    }

    public int deflate(ByteBuffer output, int flush) {
        int length = output.remaining();
        byte[] bytes = new byte[length];
        int written = deflate(bytes, 0, length, flush);
        output.put(bytes, 0, written);
        return written;
    }

    public int getAdler() {
        return nativeAdler(handle);
    }

    public int getTotalIn() {
        return (int) getBytesRead();
    }

    public long getBytesRead() {
        return bytesRead;
    }

    public int getTotalOut() {
        return (int) getBytesWritten();
    }

    public long getBytesWritten() {
        return bytesWritten;
    }

    public void reset() {
        nativeReset(handle);
        input = null;
        inputOffset = 0;
        inputLength = 0;
        finishing = false;
        paramsChanged = false;
        bytesRead = 0;
        bytesWritten = 0;
    }

    public void end() {
        if (handle != 0) {
            nativeEnd(handle);
            handle = 0;
        }
    }

    private static native long init(int level, int strategy, boolean nowrap);
    private static native int nativeDeflate(
            long handle, byte[] input, int inputOffset, int inputLength,
            byte[] output, int outputOffset, int outputLength,
            int flush, int params);
    private static native int nativeRemaining(long handle);
    private static native void nativeSetDictionary(
            long handle, byte[] dictionary, int offset, int length);
    private static native boolean nativeFinished(long handle);
    private static native int nativeAdler(long handle);
    private static native void nativeReset(long handle);
    private static native void nativeEnd(long handle);
}
