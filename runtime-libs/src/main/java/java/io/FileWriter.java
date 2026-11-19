package java.io;

public class FileWriter extends Writer {
    private final FileOutputStream output;

    public FileWriter(String fileName) throws IOException {
        output = new FileOutputStream(fileName);
    }

    public FileWriter(String fileName, boolean append) throws IOException {
        output = new FileOutputStream(fileName, append);
    }

    public FileWriter(File file) throws IOException {
        output = new FileOutputStream(file);
    }

    public FileWriter(File file, boolean append) throws IOException {
        output = new FileOutputStream(file, append);
    }

    @Override
    public void write(int character) throws IOException {
        output.write(character);
    }

    @Override
    public void write(char[] characters, int offset, int length)
            throws IOException {
        if (characters == null) throw new NullPointerException();
        if (offset < 0 || length < 0 ||
                offset > characters.length - length) {
            throw new IndexOutOfBoundsException();
        }
        output.write(new String(characters, offset, length).getBytes());
    }

    @Override
    public void write(String text, int offset, int length)
            throws IOException {
        if (text == null) throw new NullPointerException();
        output.write(text.substring(offset, offset + length).getBytes());
    }

    @Override
    public void flush() throws IOException {
        output.flush();
    }

    @Override
    public void close() throws IOException {
        output.close();
    }
}
