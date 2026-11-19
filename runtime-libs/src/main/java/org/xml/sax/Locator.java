package org.xml.sax;

/** org.xml.sax.Locator（SAX 公开接口）。 */
public interface Locator {
    String getPublicId();

    String getSystemId();

    int getLineNumber();

    int getColumnNumber();
}
