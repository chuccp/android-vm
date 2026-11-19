package org.xml.sax;

/** org.xml.sax.DTDHandler（SAX 公开接口）。 */
public interface DTDHandler {
    void notationDecl(String name, String publicId, String systemId) throws SAXException;

    void unparsedEntityDecl(String name, String publicId, String systemId,
                            String notationName) throws SAXException;
}
