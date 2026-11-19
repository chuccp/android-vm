package java.lang;

final class LiveStackFrameInfo extends StackFrameInfo implements LiveStackFrame {
    private static Object[] EMPTY_ARRAY = new Object[0];

    private static final int MODE_INTERPRETED = 0x01;
    private static final int MODE_COMPILED    = 0x02;

    LiveStackFrameInfo(StackWalker walker) {
        super(walker);
    }

    private Object[] monitors = EMPTY_ARRAY;
    private Object[] locals = EMPTY_ARRAY;
    private Object[] operands = EMPTY_ARRAY;
    private int mode = 0;

    @Override
    public Object[] getMonitors() {
        return monitors;
    }

    @Override
    public Object[] getLocals() {
        return locals;
    }

    @Override
    public Object[] getStack() {
        return operands;
    }

    @Override
    public String toString() {
        StringBuilder retVal = new StringBuilder(super.toString());
        if (mode != 0) {
            retVal.append("(");
            if ((mode & MODE_INTERPRETED) == MODE_INTERPRETED) {
                retVal.append(" interpreted ");
            }
            if ((mode & MODE_COMPILED) == MODE_COMPILED) {
                retVal.append(" compiled ");
            }
            retVal.append(")");
        }
        return retVal.toString();
    }

    static PrimitiveSlot asPrimitive(int value) {
        return new PrimitiveSlot32(value);
    }

    static PrimitiveSlot asPrimitive(long value) {
        return new PrimitiveSlot64(value);
    }

    private static class PrimitiveSlot32 extends PrimitiveSlot {
        final int value;
        PrimitiveSlot32(int value) {
            this.value = value;
        }

        @Override
        public int size() {
            return 4;
        }

        @Override
        public int intValue() {
            return value;
        }

        @Override
        public String toString() {
            return String.valueOf(value);
        }
    }

    private static class PrimitiveSlot64 extends PrimitiveSlot {
        final long value;
        PrimitiveSlot64(long value) {
            this.value = value;
        }

        @Override
        public int size() {
            return 8;
        }

        @Override
        public long longValue() {
            return value;
        }

        @Override
        public String toString() {
            return String.valueOf(value);
        }
    }
}
