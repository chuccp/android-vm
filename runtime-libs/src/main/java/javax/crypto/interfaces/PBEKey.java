package javax.crypto.interfaces;

import java.math.BigInteger;

public interface PBEKey extends javax.crypto.SecretKey {

    static final long serialVersionUID = -1430015993304333921L;

    char[] getPassword();

    byte[] getSalt();

    int getIterationCount();
}
