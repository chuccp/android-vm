package android.view;

import android.os.Parcel;
import android.os.Parcelable;

/**
 * 触摸事件（AOSP android.view.MotionEvent 公开面；§31.7 C 档 lib POJO——事件
 * 数据载体；多指针数组态）。
 */
public final class MotionEvent implements Parcelable {
    public static final int ACTION_MASK = 0xff;
    public static final int ACTION_DOWN = 0;
    public static final int ACTION_UP = 1;
    public static final int ACTION_MOVE = 2;
    public static final int ACTION_CANCEL = 3;
    public static final int ACTION_OUTSIDE = 4;
    public static final int ACTION_POINTER_DOWN = 5;
    public static final int ACTION_POINTER_UP = 6;
    public static final int ACTION_HOVER_MOVE = 7;
    public static final int ACTION_SCROLL = 8;
    public static final int ACTION_HOVER_ENTER = 9;
    public static final int ACTION_HOVER_EXIT = 10;
    public static final int ACTION_POINTER_INDEX_MASK = 0xff00;
    public static final int ACTION_POINTER_INDEX_SHIFT = 8;

    public static final int TOOL_TYPE_UNKNOWN = 0;
    public static final int TOOL_TYPE_FINGER = 1;
    public static final int TOOL_TYPE_STYLUS = 2;

    public static final int EDGE_TOP = 0x00000001;
    public static final int EDGE_BOTTOM = 0x00000002;
    public static final int EDGE_LEFT = 0x00000004;
    public static final int EDGE_RIGHT = 0x00000008;

    private static final int MAX_POINTERS = 16;

    private final float[] mX = new float[MAX_POINTERS];
    private final float[] mY = new float[MAX_POINTERS];
    private final float[] mRawX = new float[MAX_POINTERS];
    private final float[] mRawY = new float[MAX_POINTERS];
    private final int[] mPointerIds = new int[MAX_POINTERS];
    private int mPointerCount;
    private int mAction;
    private long mDownTime;
    private long mEventTime;
    private int mMetaState;
    private float mXPrecision = 1.0f;
    private float mYPrecision = 1.0f;
    private int mDeviceId = -1;
    private int mEdgeFlags;
    private int mSource = 0x1002;  // InputDevice.SOURCE_TOUCHSCREEN
    private int mFlags;
    private boolean mRecycled;

    public MotionEvent(long downTime, long eventTime, int action, int pointerCount,
                       PointerProperties[] pointerProperties, PointerCoords[] pointerCoords,
                       int metaState, int buttonState, float xPrecision, float yPrecision,
                       int deviceId, int edgeFlags, int source, int flags) {
        this.mDownTime = downTime;
        this.mEventTime = eventTime;
        this.mAction = action;
        this.mMetaState = metaState;
        this.mXPrecision = xPrecision;
        this.mYPrecision = yPrecision;
        this.mDeviceId = deviceId;
        this.mEdgeFlags = edgeFlags;
        this.mSource = source;
        this.mFlags = flags;
        this.mPointerCount = Math.min(pointerCount, MAX_POINTERS);
    }

    /** 简化构造（单指针测试面）。 */
    public MotionEvent(long downTime, long eventTime, int action, float x, float y) {
        this(downTime, eventTime, action, 1, null, null, 0, 0, 1, 1, -1, 0,
                0x1002, 0);
        this.mX[0] = x;
        this.mY[0] = y;
        this.mRawX[0] = x;
        this.mRawY[0] = y;
        this.mPointerIds[0] = 0;
    }

    /** 指针属性（AOSP MotionEvent.PointerProperties）。 */
    public static final class PointerProperties {
        public int id;
        public int toolType = TOOL_TYPE_FINGER;

        public PointerProperties() {
        }

        public PointerProperties(PointerProperties o) {
            if (o != null) {
                this.id = o.id;
                this.toolType = o.toolType;
            }
        }

        public void clear() {
            this.id = 0;
            this.toolType = TOOL_TYPE_FINGER;
        }

        public void copyFrom(PointerProperties o) {
            if (o != null) {
                this.id = o.id;
                this.toolType = o.toolType;
            }
        }
    }

    /** 指针坐标（AOSP MotionEvent.PointerCoords）。 */
    public static final class PointerCoords {
        public float orientation;
        public float pressure = 1.0f;
        public float size = 1.0f;
        public float toolMajor;
        public float toolMinor;
        public float touchMajor;
        public float touchMinor;
        public float x;
        public float y;

        public PointerCoords() {
        }

        public PointerCoords(PointerCoords o) {
            copyFrom(o);
        }

        public void clear() {
            this.orientation = 0;
            this.pressure = 1.0f;
            this.size = 1.0f;
            this.toolMajor = 0;
            this.toolMinor = 0;
            this.touchMajor = 0;
            this.touchMinor = 0;
            this.x = 0;
            this.y = 0;
        }

        public void copyFrom(PointerCoords o) {
            if (o == null) return;
            this.orientation = o.orientation;
            this.pressure = o.pressure;
            this.size = o.size;
            this.toolMajor = o.toolMajor;
            this.toolMinor = o.toolMinor;
            this.touchMajor = o.touchMajor;
            this.touchMinor = o.touchMinor;
            this.x = o.x;
            this.y = o.y;
        }

        public float getAxisValue(int axis) {
            switch (axis) {
                case 0:
                    return this.x;
                case 1:
                    return this.y;
                case 2:
                    return this.pressure;
                case 3:
                    return this.size;
                case 8:
                    return this.orientation;
                default:
                    return 0;
            }
        }

        public void setAxisValue(int axis, float value) {
            switch (axis) {
                case 0:
                    this.x = value;
                    break;
                case 1:
                    this.y = value;
                    break;
                case 2:
                    this.pressure = value;
                    break;
                case 3:
                    this.size = value;
                    break;
                case 8:
                    this.orientation = value;
                    break;
                default:
                    break;
            }
        }
    }

    public static MotionEvent obtain(long downTime, long eventTime, int action,
                                     int pointerCount, PointerProperties[] pointerProperties,
                                     PointerCoords[] pointerCoords, int metaState, int buttonState,
                                     float xPrecision, float yPrecision, int deviceId,
                                     int edgeFlags, int source, int flags) {
        return new MotionEvent(downTime, eventTime, action, pointerCount, pointerProperties,
                pointerCoords, metaState, buttonState, xPrecision, yPrecision, deviceId,
                edgeFlags, source, flags);
    }

    public static MotionEvent obtain(long downTime, long eventTime, int action, float x,
                                     float y, float pressure, float size, int metaState,
                                     float xPrecision, float yPrecision, int deviceId,
                                     int edgeFlags) {
        return new MotionEvent(downTime, eventTime, action, x, y);
    }

    public static MotionEvent obtain(long downTime, long eventTime, int action,
                                     int pointerCount, PointerCoords[] pointerCoords,
                                     int metaState, int buttonState, float xPrecision,
                                     float yPrecision, int deviceId, int edgeFlags, int source,
                                     int flags) {
        return new MotionEvent(downTime, eventTime, action, pointerCount, null, pointerCoords,
                metaState, buttonState, xPrecision, yPrecision, deviceId, edgeFlags, source,
                flags);
    }

    public static MotionEvent obtain(MotionEvent other) {
        if (other == null) return null;
        MotionEvent e = new MotionEvent(other.mDownTime, other.mEventTime, other.mAction,
                other.mPointerCount, null, null, other.mMetaState, 0, other.mXPrecision,
                other.mYPrecision, other.mDeviceId, other.mEdgeFlags, other.mSource,
                other.mFlags);
        for (int i = 0; i < other.mPointerCount; i++) {
            e.mX[i] = other.mX[i];
            e.mY[i] = other.mY[i];
            e.mRawX[i] = other.mRawX[i];
            e.mRawY[i] = other.mRawY[i];
            e.mPointerIds[i] = other.mPointerIds[i];
        }
        return e;
    }

    public static MotionEvent obtainNoHistory(MotionEvent other) {
        return obtain(other);
    }

    public MotionEvent obtain() {
        return obtain(this);
    }

    public void recycle() {
        this.mRecycled = true;
    }

    public final int getAction() {
        return this.mAction;
    }

    public final void setAction(int action) {
        this.mAction = action;
    }

    public final int getActionMasked() {
        return this.mAction & ACTION_MASK;
    }

    public final int getActionIndex() {
        return (this.mAction & ACTION_POINTER_INDEX_MASK) >> ACTION_POINTER_INDEX_SHIFT;
    }

    public final int getPointerCount() {
        return this.mPointerCount;
    }

    public final int getPointerId(int pointerIndex) {
        return this.mPointerIds[pointerIndex];
    }

    public final int findPointerIndex(int pointerId) {
        for (int i = 0; i < this.mPointerCount; i++) {
            if (this.mPointerIds[i] == pointerId) return i;
        }
        return -1;
    }

    public final float getX() {
        return this.mX[0];
    }

    public final float getY() {
        return this.mY[0];
    }

    public final float getX(int pointerIndex) {
        return this.mX[pointerIndex];
    }

    public final float getY(int pointerIndex) {
        return this.mY[pointerIndex];
    }

    public final float getRawX() {
        return this.mRawX[0];
    }

    public final float getRawY() {
        return this.mRawY[0];
    }

    public final float getRawX(int pointerIndex) {
        return this.mRawX[pointerIndex];
    }

    public final float getRawY(int pointerIndex) {
        return this.mRawY[pointerIndex];
    }

    public void offsetLocation(float deltaX, float deltaY) {
        for (int i = 0; i < this.mPointerCount; i++) {
            this.mX[i] += deltaX;
            this.mY[i] += deltaY;
        }
    }

    public void setLocation(float x, float y) {
        float dx = x - this.mX[0];
        float dy = y - this.mY[0];
        offsetLocation(dx, dy);
    }

    public final float getPressure() {
        return 1.0f;
    }

    public final float getPressure(int pointerIndex) {
        return 1.0f;
    }

    public final float getSize() {
        return 1.0f;
    }

    public final float getTouchMajor() {
        return 0;
    }

    public final float getTouchMinor() {
        return 0;
    }

    public final float getToolMajor() {
        return 0;
    }

    public final float getToolMinor() {
        return 0;
    }

    public final float getOrientation() {
        return 0;
    }

    public final int getToolType(int pointerIndex) {
        return TOOL_TYPE_FINGER;
    }

    public final int getButtonState() {
        return 0;
    }

    public final int getMetaState() {
        return this.mMetaState;
    }

    public void setMetaState(int metaState) {
        this.mMetaState = metaState;
    }

    public final int getFlags() {
        return this.mFlags;
    }

    public final long getDownTime() {
        return this.mDownTime;
    }

    public void setDownTime(long downTime) {
        this.mDownTime = downTime;
    }

    public final long getEventTime() {
        return this.mEventTime;
    }

    public final int getEdgeFlags() {
        return this.mEdgeFlags;
    }

    public final void setEdgeFlags(int flags) {
        this.mEdgeFlags = flags;
    }

    public final int getSource() {
        return this.mSource;
    }

    public final void setSource(int source) {
        this.mSource = source;
    }

    public final int getDeviceId() {
        return this.mDeviceId;
    }

    public final float getXPrecision() {
        return this.mXPrecision;
    }

    public final float getYPrecision() {
        return this.mYPrecision;
    }

    public void getPointerCoords(int pointerIndex, PointerCoords outPointerCoords) {
        if (outPointerCoords == null) return;
        outPointerCoords.x = this.mX[pointerIndex];
        outPointerCoords.y = this.mY[pointerIndex];
    }

    public void getPointerProperties(int pointerIndex, PointerProperties outPointerProperties) {
        if (outPointerProperties != null) {
            outPointerProperties.id = this.mPointerIds[pointerIndex];
        }
    }

    public final int getHistorySize() {
        return 0;
    }

    public final long getHistoricalEventTime(int pos) {
        return this.mEventTime;
    }

    public final float getHistoricalX(int pos) {
        return getX();
    }

    public final float getHistoricalY(int pos) {
        return getY();
    }

    public final float getHistoricalX(int pointerIndex, int pos) {
        return getX(pointerIndex);
    }

    public final float getHistoricalY(int pointerIndex, int pos) {
        return getY(pointerIndex);
    }

    public final boolean isTainted() {
        return false;
    }

    public final void setTainted(boolean tainted) {
    }

    @Override
    public String toString() {
        return "MotionEvent{" + this.mAction + " pointers=" + this.mPointerCount
                + " @(" + getX() + "," + getY() + ")}";
    }

    @Override
    public int describeContents() {
        return 0;
    }

    @Override
    public void writeToParcel(Parcel out, int flags) {
    }
}
