package java.security;

import java.util.*;
import java.util.regex.*;

import java.security.Provider.Service;

import sun.security.jca.*;
import sun.security.jca.GetInstance.Instance;
import sun.security.provider.SunEntries;
import sun.security.util.Debug;

public class SecureRandom extends java.util.Random {

    private static final Debug pdebug =
                        Debug.getInstance("provider", "Provider");
    private static final boolean skipDebug =
        Debug.isOn("engine=") && !Debug.isOn("securerandom");

    private Provider provider = null;

    private SecureRandomSpi secureRandomSpi = null;

    private final boolean threadSafe;

    private String algorithm;

    private static volatile SecureRandom seedGenerator;

    public SecureRandom() {

        super(0);
        getDefaultPRNG(false, null);
        this.threadSafe = getThreadSafe();
    }

    private boolean getThreadSafe() {
        if (provider == null || algorithm == null) {
            return false;
        } else {
            return Boolean.parseBoolean(provider.getProperty(
                    "SecureRandom." + algorithm + " ThreadSafe", "false"));
        }
    }

    public SecureRandom(byte[] seed) {
        super(0);
        getDefaultPRNG(true, seed);
        this.threadSafe = getThreadSafe();
    }

    private void getDefaultPRNG(boolean setSeed, byte[] seed) {
        Service prngService = null;
        String prngAlgorithm = null;
        for (Provider p : Providers.getProviderList().providers()) {

            if (p.getName().equals("SUN")) {
                prngAlgorithm = SunEntries.DEF_SECURE_RANDOM_ALGO;
                prngService = p.getService("SecureRandom", prngAlgorithm);
                break;
            } else {
                prngService = p.getDefaultSecureRandomService();
                if (prngService != null) {
                    prngAlgorithm = prngService.getAlgorithm();
                    break;
                }
            }
        }

        if (prngService == null) {
            prngAlgorithm = "SHA1PRNG";
            this.secureRandomSpi = new sun.security.provider.SecureRandom();
            this.provider = Providers.getSunProvider();
        } else {
            try {
                this.secureRandomSpi = (SecureRandomSpi)
                    prngService.newInstance(null);
                this.provider = prngService.getProvider();
            } catch (NoSuchAlgorithmException nsae) {

                throw new RuntimeException(nsae);
            }
        }
        if (setSeed) {
            this.secureRandomSpi.engineSetSeed(seed);
        }

        if (getClass() == SecureRandom.class) {
            this.algorithm = prngAlgorithm;
        }
    }

    protected SecureRandom(SecureRandomSpi secureRandomSpi,
                           Provider provider) {
        this(secureRandomSpi, provider, null);
    }

    private SecureRandom(SecureRandomSpi secureRandomSpi, Provider provider,
            String algorithm) {
        super(0);
        this.secureRandomSpi = secureRandomSpi;
        this.provider = provider;
        this.algorithm = algorithm;
        this.threadSafe = getThreadSafe();

        if (!skipDebug && pdebug != null) {
            pdebug.println("SecureRandom." + algorithm +
                " algorithm from: " + getProviderName());
        }
    }

    private String getProviderName() {
        return (provider == null) ? "(no provider)" : provider.getName();
    }

    public static SecureRandom getInstance(String algorithm)
            throws NoSuchAlgorithmException {
        Objects.requireNonNull(algorithm, "null algorithm name");
        Instance instance = GetInstance.getInstance("SecureRandom",
                SecureRandomSpi.class, algorithm);
        return new SecureRandom((SecureRandomSpi)instance.impl,
                instance.provider, algorithm);
    }

    public static SecureRandom getInstance(String algorithm, String provider)
            throws NoSuchAlgorithmException, NoSuchProviderException {
        Objects.requireNonNull(algorithm, "null algorithm name");
        Instance instance = GetInstance.getInstance("SecureRandom",
            SecureRandomSpi.class, algorithm, provider);
        return new SecureRandom((SecureRandomSpi)instance.impl,
            instance.provider, algorithm);
    }

    public static SecureRandom getInstance(String algorithm,
            Provider provider) throws NoSuchAlgorithmException {
        Objects.requireNonNull(algorithm, "null algorithm name");
        Instance instance = GetInstance.getInstance("SecureRandom",
            SecureRandomSpi.class, algorithm, provider);
        return new SecureRandom((SecureRandomSpi)instance.impl,
            instance.provider, algorithm);
    }

    public static SecureRandom getInstance(
            String algorithm, SecureRandomParameters params)
            throws NoSuchAlgorithmException {
        Objects.requireNonNull(algorithm, "null algorithm name");
        if (params == null) {
            throw new IllegalArgumentException("params cannot be null");
        }
        Instance instance = GetInstance.getInstance("SecureRandom",
                SecureRandomSpi.class, algorithm, params);
        return new SecureRandom((SecureRandomSpi)instance.impl,
                instance.provider, algorithm);
    }

    public static SecureRandom getInstance(String algorithm,
            SecureRandomParameters params, String provider)
            throws NoSuchAlgorithmException, NoSuchProviderException {
        Objects.requireNonNull(algorithm, "null algorithm name");
        if (params == null) {
            throw new IllegalArgumentException("params cannot be null");
        }
        Instance instance = GetInstance.getInstance("SecureRandom",
                SecureRandomSpi.class, algorithm, params, provider);
        return new SecureRandom((SecureRandomSpi)instance.impl,
                instance.provider, algorithm);
    }

    public static SecureRandom getInstance(String algorithm,
            SecureRandomParameters params, Provider provider)
            throws NoSuchAlgorithmException {
        Objects.requireNonNull(algorithm, "null algorithm name");
        if (params == null) {
            throw new IllegalArgumentException("params cannot be null");
        }
        Instance instance = GetInstance.getInstance("SecureRandom",
                SecureRandomSpi.class, algorithm, params, provider);
        return new SecureRandom((SecureRandomSpi)instance.impl,
                instance.provider, algorithm);
    }

    public final Provider getProvider() {
        return provider;
    }

    public String getAlgorithm() {
        return Objects.toString(algorithm, "unknown");
    }

    @Override
    public String toString() {
        return secureRandomSpi.toString();
    }

    public SecureRandomParameters getParameters() {
        return secureRandomSpi.engineGetParameters();
    }

    public void setSeed(byte[] seed) {
        if (threadSafe) {
            secureRandomSpi.engineSetSeed(seed);
        } else {
            synchronized (this) {
                secureRandomSpi.engineSetSeed(seed);
            }
        }
    }

    @Override
    public void setSeed(long seed) {

        if (seed != 0) {
            setSeed(longToByteArray(seed));
        }
    }

    @Override
    public void nextBytes(byte[] bytes) {
        if (threadSafe) {
            secureRandomSpi.engineNextBytes(bytes);
        } else {
            synchronized (this) {
                secureRandomSpi.engineNextBytes(bytes);
            }
        }
    }

    public void nextBytes(byte[] bytes, SecureRandomParameters params) {
        if (params == null) {
            throw new IllegalArgumentException("params cannot be null");
        }
        if (threadSafe) {
            secureRandomSpi.engineNextBytes(
                    Objects.requireNonNull(bytes), params);
        } else {
            synchronized (this) {
                secureRandomSpi.engineNextBytes(
                        Objects.requireNonNull(bytes), params);
            }
        }
    }

    @Override
    protected final int next(int numBits) {
        int numBytes = (numBits+7)/8;
        byte[] b = new byte[numBytes];
        int next = 0;

        nextBytes(b);
        for (int i = 0; i < numBytes; i++) {
            next = (next << 8) + (b[i] & 0xFF);
        }

        return next >>> (numBytes*8 - numBits);
    }

    public static byte[] getSeed(int numBytes) {
        SecureRandom seedGen = seedGenerator;
        if (seedGen == null) {
            seedGen = new SecureRandom();
            seedGenerator = seedGen;
        }
        return seedGen.generateSeed(numBytes);
    }

    public byte[] generateSeed(int numBytes) {
        if (numBytes < 0) {
            throw new IllegalArgumentException("numBytes cannot be negative");
        }
        if (threadSafe) {
            return secureRandomSpi.engineGenerateSeed(numBytes);
        } else {
            synchronized (this) {
                return secureRandomSpi.engineGenerateSeed(numBytes);
            }
        }
    }

    private static byte[] longToByteArray(long l) {
        byte[] retVal = new byte[8];

        for (int i = 0; i < 8; i++) {
            retVal[i] = (byte) l;
            l >>= 8;
        }

        return retVal;
    }

    private static final class StrongPatternHolder {

        private static Pattern pattern =
            Pattern.compile(
                "\\s*([\\S&&[^:,]]*)(\\:([\\S&&[^,]]*))?\\s*(\\,(.*))?");
    }

    public static SecureRandom getInstanceStrong()
            throws NoSuchAlgorithmException {

        String property = AccessController.doPrivileged(
            new PrivilegedAction<>() {
                @Override
                public String run() {
                    return Security.getProperty(
                        "securerandom.strongAlgorithms");
                }
            });

        if (property == null || property.isEmpty()) {
            throw new NoSuchAlgorithmException(
                "Null/empty securerandom.strongAlgorithms Security Property");
        }

        String remainder = property;
        while (remainder != null) {
            Matcher m;
            if ((m = StrongPatternHolder.pattern.matcher(
                    remainder)).matches()) {

                String alg = m.group(1);
                String prov = m.group(3);

                try {
                    if (prov == null) {
                        return SecureRandom.getInstance(alg);
                    } else {
                        return SecureRandom.getInstance(alg, prov);
                    }
                } catch (NoSuchAlgorithmException |
                        NoSuchProviderException e) {
                }
                remainder = m.group(5);
            } else {
                remainder = null;
            }
        }

        throw new NoSuchAlgorithmException(
            "No strong SecureRandom impls available: " + property);
    }

    public void reseed() {
        if (threadSafe) {
            secureRandomSpi.engineReseed(null);
        } else {
            synchronized (this) {
                secureRandomSpi.engineReseed(null);
            }
        }
    }

    public void reseed(SecureRandomParameters params) {
        if (params == null) {
            throw new IllegalArgumentException("params cannot be null");
        }
        if (threadSafe) {
            secureRandomSpi.engineReseed(params);
        } else {
            synchronized (this) {
                secureRandomSpi.engineReseed(params);
            }
        }
    }

    static final long serialVersionUID = 4940670005562187L;

    private byte[] state;

    private MessageDigest digest = null;

    private byte[] randomBytes;

    private int randomBytesUsed;

    private long counter;
}
