package java.io;

public class FileReader extends Reader {
    private FileInputStream input;
    private byte[] bytes;
    private int position;

    public FileReader(String fileName) throws FileNotFoundException {
        input = new FileInputStream(fileName);
    }

    public FileReader(File file) throws FileNotFoundException {
        input = new FileInputStream(file);
    }

    private void load() throws IOException {
        if (bytes != null) return;
        ByteArrayOutputStream output = new ByteArrayOutputStream();
        byte[] buffer = new byte[512];
        int count;
        while ((count = input.read(buffer, 0, buffer.length)) > 0) {
            output.write(buffer, 0, count);
        }
        input.close();
        input = null;
        bytes = output.toByteArray();
    }

    @Override
    public int read(char[] target, int offset, int length)
            throws IOException {
        load();
        if (target == null) throw new NullPointerException();
        if (offset < 0 || length < 0 ||
                offset > target.length - length) {
            throw new IndexOutOfBoundsException();
        }
        if (position >= bytes.length) return -1;
        int count = Math.min(length, bytes.length - position);
        for (int index = 0; index < count; index++) {
            target[offset + index] =
                    (char) (bytes[position + index] & 0xff);
        }
        position += count;
        return count;
    }

    @Override
    public boolean ready() throws IOException {
        load();
        return position < bytes.length;
    }

    @Override
    public void close() throws IOException {
        if (input != null) input.close();
        input = null;
        bytes = null;
    }
}
