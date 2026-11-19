package org.xml.sax;

/** org.xml.sax.Attributes（AOSP/SAX 公开接口）。 */
public interface Attributes {
    int getLength();

    String getURI(int index);

    String getLocalName(int index);

    String getQName(int index);

    String getType(int index);

    String getValue(int index);

    String getType(String uri, String localName);

    String getType(String qName);

    String getValue(String uri, String localName);

    String getValue(String qName);

    int getIndex(String uri, String localName);

    int getIndex(String qName);
}
