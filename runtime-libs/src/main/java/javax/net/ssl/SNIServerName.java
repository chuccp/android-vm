package javax.net.ssl;

import java.util.Arrays;

public abstract class SNIServerName {

    private final int type;

    private final byte[] encoded;

    private static final char[] HEXES = "0123456789ABCDEF".toCharArray();

    protected SNIServerName(int type, byte[] encoded) {
        if (type < 0) {
            throw new IllegalArgumentException(
                "Server name type cannot be less than zero");
        } else if (type > 255) {
            throw new IllegalArgumentException(
                "Server name type cannot be greater than 255");
        }
        this.type = type;

        if (encoded == null) {
            throw new NullPointerException(
                "Server name encoded value cannot be null");
        }
        this.encoded = encoded.clone();
    }

    public final int getType() {
        return type;
    }

    public final byte[] getEncoded() {
        return encoded.clone();
    }

    @Override
    public boolean equals(Object other) {
        if (this == other) {
            return true;
        }

        if (this.getClass() != other.getClass()) {
            return false;
        }

        SNIServerName that = (SNIServerName)other;
        return (this.type == that.type) &&
                    Arrays.equals(this.encoded, that.encoded);
    }

    @Override
    public int hashCode() {
        int result = 17;
        result = 31 * result + type;
        result = 31 * result + Arrays.hashCode(encoded);

        return result;
    }

    @Override
    public String toString() {
        if (type == StandardConstants.SNI_HOST_NAME) {
            return "type=host_name (0), value=" + toHexString(encoded);
        } else {
            return "type=(" + type + "), value=" + toHexString(encoded);
        }
    }

    private static String toHexString(byte[] bytes) {
        if (bytes.length == 0) {
            return "(empty)";
        }

        StringBuilder sb = new StringBuilder(bytes.length * 3 - 1);
        boolean isInitial = true;
        for (byte b : bytes) {
            if (isInitial) {
                isInitial = false;
            } else {
                sb.append(':');
            }

            int k = b & 0xFF;
            sb.append(HEXES[k >>> 4]);
            sb.append(HEXES[k & 0xF]);
        }

        return sb.toString();
    }
}
