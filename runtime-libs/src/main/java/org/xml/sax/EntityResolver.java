package org.xml.sax;

import java.io.IOException;

/** org.xml.sax.EntityResolver（SAX 公开接口）。 */
public interface EntityResolver {
    InputSource resolveEntity(String publicId, String systemId)
            throws SAXException, IOException;
}
