package javax.crypto.interfaces;

import java.math.BigInteger;

public interface DHPublicKey extends DHKey, java.security.PublicKey {

    static final long serialVersionUID = -6628103563352519193L;

    BigInteger getY();
}
