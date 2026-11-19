package android.graphics.drawable;

/**
 * 状态集（AOSP android.graphics.drawable.StateSet 辅助；§31.7 C 档 lib POJO）。
 */
public final class StateSet {
    public static final int[] WILD_CARD = new int[0];

    private StateSet() {
    }

    public static boolean isWildcard(int[] stateSetOrSpec) {
        return stateSetOrSpec == null || stateSetOrSpec.length == 0;
    }

    public static boolean stateMatches(int[] stateSpec, int[] stateSet) {
        if (isWildcard(stateSpec)) return true;
        if (stateSet == null) return false;
        for (int spec : stateSpec) {
            boolean found = false;
            if (spec == 0) break;
            for (int s : stateSet) {
                if (s == spec) {
                    found = true;
                    break;
                }
            }
            if (!found) return false;
        }
        return true;
    }
}
