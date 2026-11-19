package org.w3c.dom;

/** W3C DOM NamedNodeMap（骨架面）。 */
public interface NamedNodeMap {
    Node getNamedItem(String name);

    Node setNamedItem(Node arg) throws DOMException;

    Node removeNamedItem(String name) throws DOMException;

    Node item(int index);

    int getLength();
}
