package java.util.jar;

import java.io.FilterInputStream;
import java.io.DataOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.IOException;
import java.util.Map;
import java.util.HashMap;

import sun.nio.cs.UTF_8;

public class Manifest implements Cloneable {

    private final Attributes attr = new Attributes();

    private final Map<String, Attributes> entries = new HashMap<>();

    private final JarVerifier jv;

    public Manifest() {
        jv = null;
    }

    public Manifest(InputStream is) throws IOException {
        this(null, is);
    }

    Manifest(JarVerifier jv, InputStream is) throws IOException {
        read(is);
        this.jv = jv;
    }

    public Manifest(Manifest man) {
        attr.putAll(man.getMainAttributes());
        entries.putAll(man.getEntries());
        jv = man.jv;
    }

    public Attributes getMainAttributes() {
        return attr;
    }

    public Map<String,Attributes> getEntries() {
        return entries;
    }

    public Attributes getAttributes(String name) {
        return getEntries().get(name);
    }

    Attributes getTrustedAttributes(String name) {

        Attributes result = getAttributes(name);
        if (result != null && jv != null && ! jv.isTrustedManifestEntry(name)) {
            throw new SecurityException("Untrusted manifest entry: " + name);
        }
        return result;
    }

    public void clear() {
        attr.clear();
        entries.clear();
    }

    @SuppressWarnings("deprecation")
    public void write(OutputStream out) throws IOException {
        DataOutputStream dos = new DataOutputStream(out);

        attr.writeMain(dos);

        for (Map.Entry<String, Attributes> e : entries.entrySet()) {
            StringBuffer buffer = new StringBuffer("Name: ");
            String value = e.getKey();
            if (value != null) {
                byte[] vb = value.getBytes(UTF_8.INSTANCE);
                value = new String(vb, 0, 0, vb.length);
            }
            buffer.append(value);
            make72Safe(buffer);
            buffer.append("\r\n");
            dos.writeBytes(buffer.toString());
            e.getValue().write(dos);
        }
        dos.flush();
    }

    static void make72Safe(StringBuffer line) {
        int length = line.length();
        int index = 72;
        while (index < length) {
            line.insert(index, "\r\n ");
            index += 74;
            length += 3;
        }
        return;
    }

    public void read(InputStream is) throws IOException {

        FastInputStream fis = new FastInputStream(is);

        byte[] lbuf = new byte[512];

        attr.read(fis, lbuf);

        int ecount = 0, acount = 0;

        int asize = 2;

        int len;
        String name = null;
        boolean skipEmptyLines = true;
        byte[] lastline = null;

        while ((len = fis.readLine(lbuf)) != -1) {
            byte c = lbuf[--len];
            if (c != '\n' && c != '\r') {
                throw new IOException("manifest line too long");
            }
            if (len > 0 && lbuf[len-1] == '\r') {
                --len;
            }
            if (len == 0 && skipEmptyLines) {
                continue;
            }
            skipEmptyLines = false;

            if (name == null) {
                name = parseName(lbuf, len);
                if (name == null) {
                    throw new IOException("invalid manifest format");
                }
                if (fis.peek() == ' ') {

                    lastline = new byte[len - 6];
                    System.arraycopy(lbuf, 6, lastline, 0, len - 6);
                    continue;
                }
            } else {

                byte[] buf = new byte[lastline.length + len - 1];
                System.arraycopy(lastline, 0, buf, 0, lastline.length);
                System.arraycopy(lbuf, 1, buf, lastline.length, len - 1);
                if (fis.peek() == ' ') {

                    lastline = buf;
                    continue;
                }
                name = new String(buf, 0, buf.length, UTF_8.INSTANCE);
                lastline = null;
            }
            Attributes attr = getAttributes(name);
            if (attr == null) {
                attr = new Attributes(asize);
                entries.put(name, attr);
            }
            attr.read(fis, lbuf);
            ecount++;
            acount += attr.size();

            asize = Math.max(2, acount / ecount);

            name = null;
            skipEmptyLines = true;
        }
    }

    private String parseName(byte[] lbuf, int len) {
        if (toLower(lbuf[0]) == 'n' && toLower(lbuf[1]) == 'a' &&
            toLower(lbuf[2]) == 'm' && toLower(lbuf[3]) == 'e' &&
            lbuf[4] == ':' && lbuf[5] == ' ') {
            return new String(lbuf, 6, len - 6, UTF_8.INSTANCE);
        }
        return null;
    }

    private int toLower(int c) {
        return (c >= 'A' && c <= 'Z') ? 'a' + (c - 'A') : c;
    }

    public boolean equals(Object o) {
        if (o instanceof Manifest) {
            Manifest m = (Manifest)o;
            return attr.equals(m.getMainAttributes()) &&
                   entries.equals(m.getEntries());
        } else {
            return false;
        }
    }

    public int hashCode() {
        return attr.hashCode() + entries.hashCode();
    }

    public Object clone() {
        return new Manifest(this);
    }

    static class FastInputStream extends FilterInputStream {
        private byte buf[];
        private int count = 0;
        private int pos = 0;

        FastInputStream(InputStream in) {
            this(in, 8192);
        }

        FastInputStream(InputStream in, int size) {
            super(in);
            buf = new byte[size];
        }

        public int read() throws IOException {
            if (pos >= count) {
                fill();
                if (pos >= count) {
                    return -1;
                }
            }
            return Byte.toUnsignedInt(buf[pos++]);
        }

        public int read(byte[] b, int off, int len) throws IOException {
            int avail = count - pos;
            if (avail <= 0) {
                if (len >= buf.length) {
                    return in.read(b, off, len);
                }
                fill();
                avail = count - pos;
                if (avail <= 0) {
                    return -1;
                }
            }
            if (len > avail) {
                len = avail;
            }
            System.arraycopy(buf, pos, b, off, len);
            pos += len;
            return len;
        }

        public int readLine(byte[] b, int off, int len) throws IOException {
            byte[] tbuf = this.buf;
            int total = 0;
            while (total < len) {
                int avail = count - pos;
                if (avail <= 0) {
                    fill();
                    avail = count - pos;
                    if (avail <= 0) {
                        return -1;
                    }
                }
                int n = len - total;
                if (n > avail) {
                    n = avail;
                }
                int tpos = pos;
                int maxpos = tpos + n;
                byte c = 0;

                while (tpos < maxpos && (c = tbuf[tpos++]) != '\n' && c != '\r');
                if (c == '\r' && tpos < maxpos && tbuf[tpos] == '\n') {
                    tpos++;
                }
                n = tpos - pos;
                System.arraycopy(tbuf, pos, b, off, n);
                off += n;
                total += n;
                pos = tpos;
                c = tbuf[tpos-1];
                if (c == '\n') {
                    break;
                }
                if (c == '\r') {
                    if (count == pos) {

                        fill();
                        if (pos < count && tbuf[pos] == '\n') {
                            if (total < len) {
                                b[off++] = '\n';
                                total++;
                            } else {

                                b[off - 1] = '\n';
                            }
                            pos++;
                        }
                    }
                    break;
                }
            }
            return total;
        }

        public byte peek() throws IOException {
            if (pos == count)
                fill();
            if (pos == count)
                return -1;
            return buf[pos];
        }

        public int readLine(byte[] b) throws IOException {
            return readLine(b, 0, b.length);
        }

        public long skip(long n) throws IOException {
            if (n <= 0) {
                return 0;
            }
            long avail = count - pos;
            if (avail <= 0) {
                return in.skip(n);
            }
            if (n > avail) {
                n = avail;
            }
            pos += n;
            return n;
        }

        public int available() throws IOException {
            return (count - pos) + in.available();
        }

        public void close() throws IOException {
            if (in != null) {
                in.close();
                in = null;
                buf = null;
            }
        }

        private void fill() throws IOException {
            count = pos = 0;
            int n = in.read(buf, 0, buf.length);
            if (n > 0) {
                count = n;
            }
        }
    }
}
