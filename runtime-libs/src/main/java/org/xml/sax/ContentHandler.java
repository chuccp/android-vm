package org.xml.sax;

/** org.xml.sax.ContentHandler（SAX 公开接口）。 */
public interface ContentHandler {
    void setDocumentLocator(Locator locator);

    void startDocument() throws SAXException;

    void endDocument() throws SAXException;

    void startPrefixMapping(String prefix, String uri) throws SAXException;

    void endPrefixMapping(String prefix) throws SAXException;

    void startElement(String uri, String localName, String qName, Attributes atts)
            throws SAXException;

    void endElement(String uri, String localName, String qName) throws SAXException;

    void characters(char[] ch, int start, int length) throws SAXException;

    void ignorableWhitespace(char[] ch, int start, int length) throws SAXException;

    void processingInstruction(String target, String data) throws SAXException;

    void skippedEntity(String name) throws SAXException;
}
