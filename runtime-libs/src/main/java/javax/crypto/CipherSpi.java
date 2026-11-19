package javax.crypto;

import java.util.StringTokenizer;
import java.util.NoSuchElementException;
import java.security.AlgorithmParameters;
import java.security.Provider;
import java.security.Key;
import java.security.SecureRandom;
import java.security.NoSuchAlgorithmException;
import java.security.NoSuchProviderException;
import java.security.InvalidKeyException;
import java.security.InvalidAlgorithmParameterException;
import java.security.ProviderException;
import java.security.spec.AlgorithmParameterSpec;

import java.nio.ByteBuffer;

public abstract class CipherSpi {

    protected abstract void engineSetMode(String mode)
        throws NoSuchAlgorithmException;

    protected abstract void engineSetPadding(String padding)
        throws NoSuchPaddingException;

    protected abstract int engineGetBlockSize();

    protected abstract int engineGetOutputSize(int inputLen);

    protected abstract byte[] engineGetIV();

    protected abstract AlgorithmParameters engineGetParameters();

    protected abstract void engineInit(int opmode, Key key,
                                       SecureRandom random)
        throws InvalidKeyException;

    protected abstract void engineInit(int opmode, Key key,
                                       AlgorithmParameterSpec params,
                                       SecureRandom random)
        throws InvalidKeyException, InvalidAlgorithmParameterException;

    protected abstract void engineInit(int opmode, Key key,
                                       AlgorithmParameters params,
                                       SecureRandom random)
        throws InvalidKeyException, InvalidAlgorithmParameterException;

    protected abstract byte[] engineUpdate(byte[] input, int inputOffset,
                                           int inputLen);

    protected abstract int engineUpdate(byte[] input, int inputOffset,
                                        int inputLen, byte[] output,
                                        int outputOffset)
        throws ShortBufferException;

    protected int engineUpdate(ByteBuffer input, ByteBuffer output)
            throws ShortBufferException {
        try {
            return bufferCrypt(input, output, true);
        } catch (IllegalBlockSizeException e) {

            throw new ProviderException("Internal error in update()");
        } catch (BadPaddingException e) {

            throw new ProviderException("Internal error in update()");
        }
    }

    protected abstract byte[] engineDoFinal(byte[] input, int inputOffset,
                                            int inputLen)
        throws IllegalBlockSizeException, BadPaddingException;

    protected abstract int engineDoFinal(byte[] input, int inputOffset,
                                         int inputLen, byte[] output,
                                         int outputOffset)
        throws ShortBufferException, IllegalBlockSizeException,
               BadPaddingException;

    protected int engineDoFinal(ByteBuffer input, ByteBuffer output)
            throws ShortBufferException, IllegalBlockSizeException,
            BadPaddingException {
        return bufferCrypt(input, output, false);
    }

    static int getTempArraySize(int totalSize) {
        return Math.min(4096, totalSize);
    }

    private int bufferCrypt(ByteBuffer input, ByteBuffer output,
            boolean isUpdate) throws ShortBufferException,
            IllegalBlockSizeException, BadPaddingException {
        if ((input == null) || (output == null)) {
            throw new NullPointerException
                ("Input and output buffers must not be null");
        }
        int inPos = input.position();
        int inLimit = input.limit();
        int inLen = inLimit - inPos;
        if (isUpdate && (inLen == 0)) {
            return 0;
        }
        int outLenNeeded = engineGetOutputSize(inLen);

        if (output.remaining() < outLenNeeded) {
            throw new ShortBufferException("Need at least " + outLenNeeded
                + " bytes of space in output buffer");
        }

        boolean a1 = input.hasArray();
        boolean a2 = output.hasArray();
        int total = 0;
        byte[] inArray, outArray;
        if (a2) {
            outArray = output.array();
            int outPos = output.position();
            int outOfs = output.arrayOffset() + outPos;

            if (a1) {
                inArray = input.array();
                int inOfs = input.arrayOffset() + inPos;
                if (isUpdate) {
                    total = engineUpdate(inArray, inOfs, inLen, outArray, outOfs);
                } else {
                    total = engineDoFinal(inArray, inOfs, inLen, outArray, outOfs);
                }
                input.position(inLimit);
            } else {
                inArray = new byte[getTempArraySize(inLen)];
                do {
                    int chunk = Math.min(inLen, inArray.length);
                    if (chunk > 0) {
                        input.get(inArray, 0, chunk);
                    }
                    int n;
                    if (isUpdate || (inLen > chunk)) {
                        n = engineUpdate(inArray, 0, chunk, outArray, outOfs);
                    } else {
                        n = engineDoFinal(inArray, 0, chunk, outArray, outOfs);
                    }
                    total += n;
                    outOfs += n;
                    inLen -= chunk;
                } while (inLen > 0);
            }
            output.position(outPos + total);
        } else {
            if (a1) {
                inArray = input.array();
                int inOfs = input.arrayOffset() + inPos;
                if (isUpdate) {
                    outArray = engineUpdate(inArray, inOfs, inLen);
                } else {
                    outArray = engineDoFinal(inArray, inOfs, inLen);
                }
                input.position(inLimit);
                if (outArray != null && outArray.length != 0) {
                    output.put(outArray);
                    total = outArray.length;
                }
            } else {
                inArray = new byte[getTempArraySize(inLen)];
                do {
                    int chunk = Math.min(inLen, inArray.length);
                    if (chunk > 0) {
                        input.get(inArray, 0, chunk);
                    }
                    int n;
                    if (isUpdate || (inLen > chunk)) {
                        outArray = engineUpdate(inArray, 0, chunk);
                    } else {
                        outArray = engineDoFinal(inArray, 0, chunk);
                    }
                    if (outArray != null && outArray.length != 0) {
                        output.put(outArray);
                        total += outArray.length;
                    }
                    inLen -= chunk;
                } while (inLen > 0);
            }
        }
        return total;
    }

    protected byte[] engineWrap(Key key)
        throws IllegalBlockSizeException, InvalidKeyException
    {
        throw new UnsupportedOperationException();
    }

    protected Key engineUnwrap(byte[] wrappedKey,
                               String wrappedKeyAlgorithm,
                               int wrappedKeyType)
        throws InvalidKeyException, NoSuchAlgorithmException
    {
        throw new UnsupportedOperationException();
    }

    protected int engineGetKeySize(Key key)
        throws InvalidKeyException
    {
        throw new UnsupportedOperationException();
    }

    protected void engineUpdateAAD(byte[] src, int offset, int len) {
        throw new UnsupportedOperationException(
            "The underlying Cipher implementation "
            +  "does not support this method");
    }

    protected void engineUpdateAAD(ByteBuffer src) {
        throw new UnsupportedOperationException(
            "The underlying Cipher implementation "
            +  "does not support this method");
    }
}
