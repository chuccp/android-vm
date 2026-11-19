package com.coke.target;

/** Plain JavaBean used by the Fastjson guest compatibility fixture. */
public final class RuntimeFastJsonValue {
    public String displayName;
    public int count;

    public RuntimeFastJsonValue() {
    }

    public String getDisplayName() {
        return displayName;
    }

    public void setDisplayName(String displayName) {
        this.displayName = displayName;
    }

    public int getCount() {
        return count;
    }

    public void setCount(int count) {
        this.count = count;
    }
}
