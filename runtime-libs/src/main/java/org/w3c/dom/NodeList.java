package org.w3c.dom;

/** W3C DOM NodeList（骨架面）。 */
public interface NodeList {
    Node item(int index);

    int getLength();
}
