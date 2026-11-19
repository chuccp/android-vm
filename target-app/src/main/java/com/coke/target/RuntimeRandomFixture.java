package com.coke.target;

import java.security.SecureRandom;
import java.util.Random;

public final class RuntimeRandomFixture {
    public int randomFlow() {
        Random first = new Random(12345L);
        Random second = new Random(12345L);
        int[] expected = {
                1553932502, -2090749135, -287790814, -355989640,
        };
        for (int value : expected) {
            if (first.nextInt() != value || second.nextInt() != value) {
                return -1;
            }
        }
        if (first.nextInt(1000) != second.nextInt(1000) ||
                first.nextLong() != second.nextLong() ||
                first.nextBoolean() != second.nextBoolean() ||
                first.nextDouble() != second.nextDouble()) {
            return -2;
        }
        float nextFloat = first.nextFloat();
        if (nextFloat < 0.0f || nextFloat >= 1.0f) {
            return -3;
        }
        double gaussian = first.nextGaussian();
        if (gaussian != gaussian || gaussian > 10.0 ||
                gaussian < -10.0) {
            return -4;
        }

        first.setSeed(12345L);
        if (first.nextInt() != expected[0]) {
            return -5;
        }
        byte[] firstBytes = new byte[16];
        byte[] secondBytes = new byte[16];
        first.setSeed(99L);
        second.setSeed(99L);
        first.nextBytes(firstBytes);
        second.nextBytes(secondBytes);
        for (int index = 0; index < firstBytes.length; index++) {
            if (firstBytes[index] != secondBytes[index]) return -6;
        }

        SecureRandom secure = new SecureRandom();
        int secureValue = secure.nextInt();
        if (secure.nextInt(7) < 0) return -7;
        if (Math.abs(-2.5d) != 2.5d ||
                Math.min(1.5d, 2.5d) != 1.5d ||
                Math.max(1.5d, 2.5d) != 2.5d) {
            return -9;
        }
        return secureValue == Integer.MIN_VALUE ? -8 : 97;
    }
}
