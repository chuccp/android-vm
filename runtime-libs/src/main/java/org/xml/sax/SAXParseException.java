package org.xml.sax;

/** org.xml.sax.SAXParseException（带位置的 SAX 异常）。 */
public class SAXParseException extends SAXException {
    private final String mPublicId;
    private final String mSystemId;
    private final int mLineNumber;
    private final int mColumnNumber;

    public SAXParseException(String message, Locator locator) {
        super(message);
        this.mPublicId = locator != null ? locator.getPublicId() : null;
        this.mSystemId = locator != null ? locator.getSystemId() : null;
        this.mLineNumber = locator != null ? locator.getLineNumber() : -1;
        this.mColumnNumber = locator != null ? locator.getColumnNumber() : -1;
    }

    public SAXParseException(String message, String publicId, String systemId, int lineNumber,
                             int columnNumber) {
        super(message);
        this.mPublicId = publicId;
        this.mSystemId = systemId;
        this.mLineNumber = lineNumber;
        this.mColumnNumber = columnNumber;
    }

    public String getPublicId() {
        return mPublicId;
    }

    public String getSystemId() {
        return mSystemId;
    }

    public int getLineNumber() {
        return mLineNumber;
    }

    public int getColumnNumber() {
        return mColumnNumber;
    }
}
