package sun.security.provider;

import java.nio.ByteBuffer;
import java.security.InvalidAlgorithmParameterException;
import java.security.InvalidKeyException;
import java.security.Key;
import java.security.MessageDigest;
import java.security.spec.AlgorithmParameterSpec;

import javax.crypto.MacSpi;

/**
 * HMAC 核心（RFC 2104；OpenJDK HmacCore 同型）：内/外填充 + 摘要复用。
 * SHA-1/SHA-256 由 SUN provider 的 Mac 服务登记（HmacSHA1/HmacSHA256）。
 */
abstract class HmacCore extends MacSpi {
    private static final int BLOCK_SIZE = 64;

    private final String digestAlg;
    private final int macLen;
    private MessageDigest md;
    private byte[] ipad;
    private byte[] opad;

    HmacCore(String digestAlg, int macLen) {
        this.digestAlg = digestAlg;
        this.macLen = macLen;
    }

    @Override
    protected int engineGetMacLength() {
        return macLen;
    }

    @Override
    protected void engineInit(Key key, AlgorithmParameterSpec params)
            throws InvalidKeyException, InvalidAlgorithmParameterException {
        if (key == null) {
            throw new InvalidKeyException("No key given");
        }
        try {
            md = MessageDigest.getInstance(digestAlg);
        } catch (Exception e) {
            throw new InvalidKeyException("digest unavailable: " + digestAlg, e);
        }
        byte[] k = key.getEncoded();
        if (k == null) {
            throw new InvalidKeyException("Key not encodable");
        }
        if (k.length > BLOCK_SIZE) {
            md.reset();
            md.update(k);
            k = md.digest();
        }
        ipad = new byte[BLOCK_SIZE];
        opad = new byte[BLOCK_SIZE];
        for (int i = 0; i < BLOCK_SIZE; i++) {
            byte b = i < k.length ? k[i] : 0;
            ipad[i] = (byte) (b ^ 0x36);
            opad[i] = (byte) (b ^ 0x5c);
        }
        md.reset();
        md.update(ipad);
    }

    @Override
    protected void engineUpdate(byte input) {
        md.update(input);
    }

    @Override
    protected void engineUpdate(byte[] input, int offset, int len) {
        md.update(input, offset, len);
    }

    @Override
    protected void engineUpdate(ByteBuffer input) {
        md.update(input);
    }

    @Override
    protected byte[] engineDoFinal() {
        byte[] inner = md.digest();
        md.reset();
        md.update(opad);
        md.update(inner);
        byte[] out = md.digest();
        md.reset();
        md.update(ipad);
        return out;
    }

    @Override
    protected void engineReset() {
        if (md != null) {
            md.reset();
            md.update(ipad);
        }
    }
}
