package javax.crypto;

import java.io.InputStream;
import java.io.FilterInputStream;
import java.io.IOException;
import javax.crypto.BadPaddingException;
import javax.crypto.IllegalBlockSizeException;

public class CipherInputStream extends FilterInputStream {

    private Cipher cipher;

    private InputStream input;

    private byte[] ibuffer = new byte[512];

    private boolean done = false;

    private byte[] obuffer;

    private int ostart = 0;

    private int ofinish = 0;

    private boolean closed = false;

    private int getMoreData() throws IOException {
        if (done) return -1;
        int readin = input.read(ibuffer);
        if (readin == -1) {
            done = true;
            try {
                obuffer = cipher.doFinal();
            } catch (IllegalBlockSizeException | BadPaddingException e) {
                obuffer = null;
                throw new IOException(e);
            }
            if (obuffer == null)
                return -1;
            else {
                ostart = 0;
                ofinish = obuffer.length;
                return ofinish;
            }
        }
        try {
            obuffer = cipher.update(ibuffer, 0, readin);
        } catch (IllegalStateException e) {
            obuffer = null;
            throw e;
        }
        ostart = 0;
        if (obuffer == null)
            ofinish = 0;
        else ofinish = obuffer.length;
        return ofinish;
    }

    public CipherInputStream(InputStream is, Cipher c) {
        super(is);
        input = is;
        cipher = c;
    }

    protected CipherInputStream(InputStream is) {
        super(is);
        input = is;
        cipher = new NullCipher();
    }

    public int read() throws IOException {
        if (ostart >= ofinish) {

            int i = 0;
            while (i == 0) i = getMoreData();
            if (i == -1) return -1;
        }
        return ((int) obuffer[ostart++] & 0xff);
    };

    public int read(byte b[]) throws IOException {
        return read(b, 0, b.length);
    }

    public int read(byte b[], int off, int len) throws IOException {
        if (ostart >= ofinish) {

            int i = 0;
            while (i == 0) i = getMoreData();
            if (i == -1) return -1;
        }
        if (len <= 0) {
            return 0;
        }
        int available = ofinish - ostart;
        if (len < available) available = len;
        if (b != null) {
            System.arraycopy(obuffer, ostart, b, off, available);
        }
        ostart = ostart + available;
        return available;
    }

    public long skip(long n) throws IOException {
        int available = ofinish - ostart;
        if (n > available) {
            n = available;
        }
        if (n < 0) {
            return 0;
        }
        ostart += n;
        return n;
    }

    public int available() throws IOException {
        return (ofinish - ostart);
    }

    public void close() throws IOException {
        if (closed) {
            return;
        }

        closed = true;
        input.close();

        if (!done) {
            try {
                cipher.doFinal();
            }
            catch (BadPaddingException | IllegalBlockSizeException ex) {

            }
        }
        ostart = 0;
        ofinish = 0;
    }

    public boolean markSupported() {
        return false;
    }
}
