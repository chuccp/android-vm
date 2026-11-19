package com.coke.target;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicReference;

/**
 * DEX fixture for standard Java collection and string semantics.
 */
public final class RuntimeCollections {
    private RuntimeCollections() {
    }

    public static int run() {
        return mapFlow()
                + listFlow()
                + concurrentFlow()
                + atomicFlow()
                + arrayFlow()
                + wideArrayFlow()
                + arrayListFlow()
                + stringFlow();
    }

    public static int mapFlow() {
        int score = 0;

        Map<String, String> values = new HashMap<>(4);
        values.put("a", "one");
        values.put("b", "two");
        if ("one".equals(values.get("a"))) {
            score += 1;
        }
        if (values.containsKey("b")) {
            score += 2;
        }
        if ("one".equals(values.remove("a"))) {
            score += 4;
        }
        if (values.size() == 1) {
            score += 8;
        }
        return score;
    }

    public static int listFlow() {
        return listLengthFlow() + listPrefixFlow();
    }

    public static int listLengthFlow() {
        int score = 0;
        List<String> names = new ArrayList<>(2);
        names.add("alpha");
        names.add("beta");
        for (String name : names) {
            score += name.length();
        }
        return score;
    }

    public static int listPrefixFlow() {
        List<String> names = new ArrayList<>(2);
        names.add("alpha");
        names.add("beta");
        if (names.get(0).startsWith("a")) {
            return 16;
        }
        return 0;
    }

    public static int concurrentFlow() {
        ConcurrentHashMap<String, Integer> concurrent = new ConcurrentHashMap<>();
        concurrent.put("x", 7);
        if (concurrent.get("x") == 7) {
            return 32;
        }
        return 0;
    }

    public static int atomicFlow() {
        int score = 0;
        AtomicReference<String> reference = new AtomicReference<>();
        if (reference.compareAndSet(null, "ready")) {
            score += 64;
        }
        if ("ready".equals(reference.get())) {
            score += 128;
        }
        return score;
    }

    public static boolean atomicCasFlow() {
        AtomicReference<String> reference = new AtomicReference<>();
        return reference.compareAndSet(null, "ready");
    }

    public static int atomicSetFlow() {
        AtomicReference<String> reference = new AtomicReference<>();
        reference.set("ready");
        return "ready".equals(reference.get()) ? 1 : 0;
    }

    public static int atomicInitialNullFlow() {
        AtomicReference<String> reference = new AtomicReference<>();
        return reference.get() == null ? 1 : 0;
    }

    public static int atomicInitialFlow() {
        AtomicReference<String> reference = new AtomicReference<>("ready");
        return "ready".equals(reference.get()) ? 1 : 0;
    }

    public static int arrayFlow() {
        int[] source = {1, 2, 3};
        int[] copied = new int[4];
        System.arraycopy(source, 1, copied, 1, 2);
        return copied[1] + copied[2];
    }

    public static int wideArrayFlow() {
        long[] values = {2L, 3L, 4L};
        long total = 0;
        for (long value : values) {
            total += value;
        }
        return (int) (total + values.length);
    }

    public static int arrayListFlow() {
        return arraySortFlow() + asListFlow() + listToArrayFlow();
    }

    public static int arraySortFlow() {
        int[] sorted = {3, 1, 2};
        Arrays.sort(sorted);
        return sorted[0] == 1 && sorted[2] == 3 ? 1 : 0;
    }

    public static int asListFlow() {
        List<Integer> values = Arrays.asList(2, 4, 6);
        int total = 0;
        for (int value : values) {
            total += value;
        }
        return total == 12 ? 2 : 0;
    }

    public static int listToArrayFlow() {
        List<String> names = new ArrayList<>();
        names.add("a");
        names.add("b");
        String[] array = names.toArray(new String[0]);
        int score = array.length == 2 ? 4 : 0;
        if ("a".equals(array[0])) {
            score += 8;
        }
        return score;
    }

    public static int listToArrayFirstLengthFlow() {
        List<String> names = new ArrayList<>();
        names.add("a");
        names.add("b");
        String[] array = names.toArray(new String[0]);
        return array[0].length();
    }

    public static int stringFlow() {
        int score = 0;
        String text = "  VM-Collections  ".trim().toLowerCase();
        if (text.startsWith("vm-")) {
            score += 512;
        }
        if (text.contains("collections")) {
            score += 1024;
        }
        return score;
    }
}
