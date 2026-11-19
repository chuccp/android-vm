package android.util;

/**
 * android.util.StateSet（AOSP 公开面）：View 状态集匹配辅助。
 *
 * <p>NOTHING/WILD_CARD 静态实例与匹配算法按 AOSP 语义（负值通配）。
 */
public class StateSet {
    public static final int[] NOTHING = new int[0];
    public static final int[] WILD_CARD = new int[1];

    public static boolean isWildCard(int[] stateSet) {
        return stateSet == null || stateSet.length == 0;
    }

    public static boolean stateSetMatches(int[] stateSpec, int[] stateSet) {
        if (stateSet == null) {
            return stateSpec == null || stateSpec.length == 0;
        }
        int stateSpecSize = stateSpec == null ? 0 : stateSpec.length;
        int stateSetSize = stateSet.length;
        for (int i = 0; i < stateSpecSize; i++) {
            int stateSpecState = stateSpec[i];
            if (stateSpecState == 0) {
                return i == 0;
            }
            final boolean mustMatch = stateSpecState > 0;
            final int state = mustMatch ? stateSpecState : -stateSpecState;
            boolean found = false;
            for (int j = 0; j < stateSetSize; j++) {
                if (state == stateSet[j]) {
                    found = true;
                    break;
                }
            }
            if (mustMatch != found) {
                return false;
            }
        }
        return true;
    }

    public static boolean stateSetMatches(int[] stateSpec, int state) {
        int stateSpecSize = stateSpec == null ? 0 : stateSpec.length;
        for (int i = 0; i < stateSpecSize; i++) {
            int stateSpecState = stateSpec[i];
            if (stateSpecState == 0) {
                return i == 0;
            }
            if (stateSpecState > 0) {
                if (state != stateSpecState) {
                    return false;
                }
            } else {
                if (state == -stateSpecState) {
                    return false;
                }
            }
        }
        return true;
    }

    public static int[] trimStateSet(int[] states, int newSize) {
        if (states.length == newSize) {
            return states;
        }
        int[] trimmed = new int[newSize];
        System.arraycopy(states, 0, trimmed, 0, newSize);
        return trimmed;
    }

    public static String dump(int[] states) {
        StringBuilder sb = new StringBuilder();
        for (int state : states) {
            sb.append(state).append(' ');
        }
        return sb.toString();
    }
}
