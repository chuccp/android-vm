package org.xml.sax.helpers;

import java.io.IOException;

import org.xml.sax.Attributes;
import org.xml.sax.ContentHandler;
import org.xml.sax.DTDHandler;
import org.xml.sax.EntityResolver;
import org.xml.sax.ErrorHandler;
import org.xml.sax.InputSource;
import org.xml.sax.Locator;
import org.xml.sax.SAXException;
import org.xml.sax.SAXParseException;

/**
 * org.xml.sax.helpers.DefaultHandler（SAX 公开面：全 no-op 基类）。
 *
 * <p>常见用法是 guest 类继承它只覆写少数回调；其余回调由本类空实现承接
 * （AOSP 同款）。
 */
public class DefaultHandler
        implements EntityResolver, DTDHandler, ContentHandler, ErrorHandler {

    public DefaultHandler() {
    }

    // ---- EntityResolver ----

    @Override
    public InputSource resolveEntity(String publicId, String systemId)
            throws IOException, SAXException {
        return null;
    }

    // ---- DTDHandler ----

    @Override
    public void notationDecl(String name, String publicId, String systemId)
            throws SAXException {
    }

    @Override
    public void unparsedEntityDecl(String name, String publicId, String systemId,
                                   String notationName) throws SAXException {
    }

    // ---- ContentHandler ----

    @Override
    public void setDocumentLocator(Locator locator) {
    }

    @Override
    public void startDocument() throws SAXException {
    }

    @Override
    public void endDocument() throws SAXException {
    }

    @Override
    public void startPrefixMapping(String prefix, String uri) throws SAXException {
    }

    @Override
    public void endPrefixMapping(String prefix) throws SAXException {
    }

    @Override
    public void startElement(String uri, String localName, String qName, Attributes atts)
            throws SAXException {
    }

    @Override
    public void endElement(String uri, String localName, String qName) throws SAXException {
    }

    @Override
    public void characters(char[] ch, int start, int length) throws SAXException {
    }

    @Override
    public void ignorableWhitespace(char[] ch, int start, int length) throws SAXException {
    }

    @Override
    public void processingInstruction(String target, String data) throws SAXException {
    }

    @Override
    public void skippedEntity(String name) throws SAXException {
    }

    // ---- ErrorHandler ----

    @Override
    public void warning(SAXParseException e) throws SAXException {
    }

    @Override
    public void error(SAXParseException e) throws SAXException {
    }

    @Override
    public void fatalError(SAXParseException e) throws SAXException {
    }
}
