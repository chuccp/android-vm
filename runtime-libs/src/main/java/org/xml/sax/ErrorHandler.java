package org.xml.sax;

/** org.xml.sax.ErrorHandler（SAX 公开接口）。 */
public interface ErrorHandler {
    void warning(SAXParseException exception) throws SAXException;

    void error(SAXParseException exception) throws SAXException;

    void fatalError(SAXParseException exception) throws SAXException;
}
