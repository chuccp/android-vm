package java.nio.charset;

import java.nio.BufferOverflowException;
import java.nio.BufferUnderflowException;

public class CoderResult {

    private static final int CR_UNDERFLOW  = 0;
    private static final int CR_OVERFLOW   = 1;
    private static final int CR_ERROR_MIN  = 2;
    private static final int CR_MALFORMED  = 2;
    private static final int CR_UNMAPPABLE = 3;

    private static final String[] names
        = { "UNDERFLOW", "OVERFLOW", "MALFORMED", "UNMAPPABLE" };

    private final int type;
    private final int length;

    private CoderResult(int type, int length) {
        this.type = type;
        this.length = length;
    }

    public String toString() {
        String nm = names[type];
        return isError() ? nm + "[" + length + "]" : nm;
    }

    public boolean isUnderflow() {
        return (type == CR_UNDERFLOW);
    }

    public boolean isOverflow() {
        return (type == CR_OVERFLOW);
    }

    public boolean isError() {
        return (type >= CR_ERROR_MIN);
    }

    public boolean isMalformed() {
        return (type == CR_MALFORMED);
    }

    public boolean isUnmappable() {
        return (type == CR_UNMAPPABLE);
    }

    public int length() {
        if (!isError())
            throw new UnsupportedOperationException();
        return length;
    }

    public static final CoderResult UNDERFLOW
        = new CoderResult(CR_UNDERFLOW, 0);

    public static final CoderResult OVERFLOW
        = new CoderResult(CR_OVERFLOW, 0);

    private static final CoderResult[] malformed4 = new CoderResult[] {
        new CoderResult(CR_MALFORMED, 1),
        new CoderResult(CR_MALFORMED, 2),
        new CoderResult(CR_MALFORMED, 3),
        new CoderResult(CR_MALFORMED, 4),
    };

    public static CoderResult malformedForLength(int length) {
        if (length <= 0)
            throw new IllegalArgumentException("Non-positive length");
        if (length <= 4)
            return malformed4[length - 1];
        return new CoderResult(CR_MALFORMED, length);
    }

    private static final CoderResult[] unmappable4 = new CoderResult[] {
        new CoderResult(CR_UNMAPPABLE, 1),
        new CoderResult(CR_UNMAPPABLE, 2),
        new CoderResult(CR_UNMAPPABLE, 3),
        new CoderResult(CR_UNMAPPABLE, 4),
    };

    public static CoderResult unmappableForLength(int length) {
        if (length <= 0)
            throw new IllegalArgumentException("Non-positive length");
        if (length <= 4)
            return unmappable4[length - 1];
        return new CoderResult(CR_UNMAPPABLE, length);
    }

    public void throwException()
        throws CharacterCodingException
    {
        switch (type) {
        case CR_UNDERFLOW:   throw new BufferUnderflowException();
        case CR_OVERFLOW:    throw new BufferOverflowException();
        case CR_MALFORMED:   throw new MalformedInputException(length);
        case CR_UNMAPPABLE:  throw new UnmappableCharacterException(length);
        default:
            assert false;
        }
    }

}
