package com.coke.target;

import java.security.MessageDigest;

public final class RuntimeDigestFixture {
    private static boolean matches(byte[] actual, int... expected) {
        if (actual.length != expected.length) return false;
        for (int index = 0; index < expected.length; index++) {
            if ((actual[index] & 0xff) != expected[index]) return false;
        }
        return true;
    }

    public int digestFlow() throws Exception {
        byte[] abc = new byte[] {'a', 'b', 'c'};

        MessageDigest sha1 = MessageDigest.getInstance("SHA-1");
        sha1.update(abc);
        byte[] sha1Result = sha1.digest();
        if (!matches(sha1Result,
                0xa9, 0x99, 0x3e, 0x36, 0x47, 0x06, 0x81, 0x6a,
                0xba, 0x3e, 0x25, 0x71, 0x78, 0x50, 0xc2, 0x6c,
                0x9c, 0xd0, 0xd8, 0x9d)) {
            return -1;
        }

        MessageDigest sha256 = MessageDigest.getInstance("SHA-256");
        sha256.update(abc, 0, abc.length);
        byte[] sha256Result = sha256.digest();
        if (!matches(sha256Result,
                0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
                0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
                0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
                0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad)) {
            return -2;
        }

        MessageDigest md5 = MessageDigest.getInstance("MD5");
        md5.update(abc);
        byte[] md5Result = md5.digest();
        if (!matches(md5Result,
                0x90, 0x01, 0x50, 0x98, 0x3c, 0xd2, 0x4f, 0xb0,
                0xd6, 0x96, 0x3f, 0x7d, 0x28, 0xe1, 0x7f, 0x72)) {
            return -3;
        }
        if (!MessageDigest.isEqual(sha1Result, sha1Result)) {
            return -4;
        }
        if (sha1.getDigestLength() != 20 ||
                sha256.getDigestLength() != 32 ||
                md5.getDigestLength() != 16) {
            return -5;
        }
        if (!"SHA-1".equals(sha1.getAlgorithm())) {
            return -6;
        }

        sha1.update((byte) 'x');
        sha1.reset();
        sha1.update(abc);
        if (!matches(sha1.digest(),
                0xa9, 0x99, 0x3e, 0x36, 0x47, 0x06, 0x81, 0x6a,
                0xba, 0x3e, 0x25, 0x71, 0x78, 0x50, 0xc2, 0x6c,
                0x9c, 0xd0, 0xd8, 0x9d)) {
            return -7;
        }
        return 95;
    }
}
