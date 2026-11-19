package java.util.zip;

import java.nio.ByteBuffer;

public class Inflater {
    private long handle;
    private byte[] input;
    private int inputOffset;
    private int inputLength;
    private long bytesRead;
    private long bytesWritten;

    public Inflater(boolean nowrap) {
        handle = init(nowrap);
    }

    public Inflater() {
        this(false);
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

    public void setDictionary(byte[] dictionary) {
        setDictionary(dictionary, 0, dictionary.length);
    }

    public void setDictionary(
            byte[] dictionary, int offset, int length) {
        nativeSetDictionary(handle, dictionary, offset, length);
    }

    public int getRemaining() {
        return inputLength;
    }

    public boolean needsInput() {
        return inputLength == 0;
    }

    public boolean needsDictionary() {
        return nativeNeedsDictionary(handle);
    }

    public boolean finished() {
        return nativeFinished(handle);
    }

    public int inflate(byte[] output) throws DataFormatException {
        return inflate(output, 0, output.length);
    }

    public int inflate(byte[] output, int offset, int length)
            throws DataFormatException {
        if (offset < 0 || length < 0 ||
                offset > output.length - length) {
            throw new ArrayIndexOutOfBoundsException();
        }
        int written = nativeInflate(
                handle,
                input, inputOffset, inputLength,
                output, offset, length);
        if (written < 0) throw new DataFormatException();
        int consumed = inputLength - nativeRemaining(handle);
        inputOffset += consumed;
        inputLength = nativeRemaining(handle);
        bytesRead += consumed;
        bytesWritten += written;
        return written;
    }

    public int inflate(ByteBuffer output) throws DataFormatException {
        int length = output.remaining();
        byte[] bytes = new byte[length];
        int written = inflate(bytes, 0, length);
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
        bytesRead = 0;
        bytesWritten = 0;
    }

    public void end() {
        if (handle != 0) {
            nativeEnd(handle);
            handle = 0;
        }
    }

    private static native long init(boolean nowrap);
    private static native int nativeInflate(
            long handle, byte[] input, int inputOffset, int inputLength,
            byte[] output, int outputOffset, int outputLength);
    private static native void nativeSetDictionary(
            long handle, byte[] dictionary, int offset, int length);
    private static native int nativeRemaining(long handle);
    private static native boolean nativeNeedsDictionary(long handle);
    private static native boolean nativeFinished(long handle);
    private static native int nativeAdler(long handle);
    private static native void nativeReset(long handle);
    private static native void nativeEnd(long handle);
}
