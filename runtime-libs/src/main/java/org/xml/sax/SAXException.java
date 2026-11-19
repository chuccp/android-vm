package org.xml.sax;

/** org.xml.sax.SAXException（SAX 公开异常面）。 */
public class SAXException extends Exception {
    private final Exception mException;

    public SAXException() {
        this.mException = null;
    }

    public SAXException(String message) {
        super(message);
        this.mException = null;
    }

    public SAXException(Exception e) {
        super(e == null ? null : e.toString());
        this.mException = e;
    }

    public SAXException(String message, Exception e) {
        super(message, e);
        this.mException = e;
    }

    public Exception getException() {
        return mException;
    }

    @Override
    public String getMessage() {
        return super.getMessage();
    }

    @Override
    public String toString() {
        return super.toString();
    }
}
