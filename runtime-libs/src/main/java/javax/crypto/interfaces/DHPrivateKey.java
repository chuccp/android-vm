package javax.crypto.interfaces;

import java.math.BigInteger;

public interface DHPrivateKey extends DHKey, java.security.PrivateKey {

    static final long serialVersionUID = 2211791113380396553L;

    BigInteger getX();
}
