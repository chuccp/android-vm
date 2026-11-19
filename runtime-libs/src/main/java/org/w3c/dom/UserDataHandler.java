package org.w3c.dom;

/** W3C DOM UserDataHandler（骨架面）。 */
public interface UserDataHandler {
    short NODE_CLONED = 1;
    short NODE_IMPORTED = 2;
    short NODE_DELETED = 3;
    short NODE_RENAMED = 4;
    short NODE_ADOPTED = 5;

    void handle(short operation, String key, Object data, Node src, Node dst);
}
