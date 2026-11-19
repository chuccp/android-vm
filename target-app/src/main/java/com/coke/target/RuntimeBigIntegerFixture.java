package com.coke.target;

import java.math.BigInteger;

public final class RuntimeBigIntegerFixture {
    private static boolean is(String expected, BigInteger actual) {
        return expected.equals(actual.toString());
    }

    public int bigIntegerFlow() {
        BigInteger a = new BigInteger(
                "123456789012345678901234567890");
        BigInteger b = new BigInteger("-98765432109876543210");

        if (!is("123456788913580246791358024680", a.add(b))) {
            return -1;
        }
        if (!is("123456789111111111011111111100", a.subtract(b))) {
            return -2;
        }
        if (!is("-12193263113702179522496570642237463801111263526900",
                a.multiply(b))) {
            return -3;
        }
        if (!is("-1249999988", a.divide(b))) {
            return -4;
        }
        if (!is("60185185207253086410", a.remainder(b))) {
            return -5;
        }
        if (!is("64616", a.mod(BigInteger.valueOf(1234567)))) {
            return -6;
        }
        if (!is("90", a.gcd(b))) {
            return -7;
        }
        if (!is("9754610579850632525677488187778997104100",
                b.pow(2))) {
            return -8;
        }
        if (!is("1011358015589135801558913580154880",
                a.shiftLeft(13))) {
            return -9;
        }
        if (!is("964506164158950616415895061", a.shiftRight(7))) {
            return -10;
        }
        if (!is("-3", BigInteger.valueOf(-5).shiftRight(1))) {
            return -11;
        }
        if (!is("210", a.and(BigInteger.valueOf(255)))) {
            return -12;
        }
        if (!is("123456789012345678901234571986",
                a.or(BigInteger.valueOf(4096)))) {
            return -13;
        }
        if (!is("123456789012345678901234567725",
                a.xor(BigInteger.valueOf(255)))) {
            return -14;
        }
        if (!is("123456789012345678901234567680",
                a.andNot(BigInteger.valueOf(255)))) {
            return -15;
        }
        if (!is("123456789012345678901234567888", a.clearBit(1))) {
            return -16;
        }
        if (!is("123456789012345678901234567888", a.flipBit(1))) {
            return -17;
        }
        if (!is("123456789012345678901234567922", a.setBit(5))) {
            return -18;
        }
        if (!"18ee90ff6c373e0ee4e3f0ad2".equals(a.toString(16))) {
            return -19;
        }
        if (!is("123456789012345678901234567890",
                new BigInteger(a.toByteArray()))) {
            return -20;
        }
        if (!is("-5", new BigInteger(
                new byte[] {(byte) 0xfb}))) {
            return -21;
        }
        BigInteger smaller = new BigInteger("999999999999999999999");
        if (a.compareTo(b) <= 0 ||
                !is("999999999999999999999", a.min(smaller)) ||
                !is("123456789012345678901234567890",
                        a.max(smaller))) {
            return -22;
        }
        if (!is("-123456789012345678901234567890", a.negate()) ||
                !is("123456789012345678901234567890",
                        a.negate().abs())) {
            return -23;
        }
        BigInteger prime = new BigInteger("32416190071");
        BigInteger composite = prime.multiply(BigInteger.valueOf(3));
        if (!prime.isProbablePrime(20) ||
                composite.isProbablePrime(20)) {
            return -24;
        }
        if (!a.testBit(1) || !a.testBit(96)) {
            return -25;
        }
        if (!is("-123456789012345678901234567891", a.not())) {
            return -26;
        }
        if (a.bitLength() != 97 || a.bitCount() != 54 ||
                a.signum() != 1 || b.signum() != -1 ||
                a.intValue() != 1312754386 ||
                b.longValue() != -6531711741328785130L ||
                a.shortValue() != 2770 || a.byteValue() != -46) {
            return -27;
        }
        if (!is("255", new BigInteger("ff", 16)) ||
                !is("-1", new BigInteger(
                        -1, new byte[] {1}))) {
            return -28;
        }
        BigInteger negative = BigInteger.valueOf(-5);
        if (!is("3", negative.and(BigInteger.valueOf(3))) ||
                !is("-5", negative.or(BigInteger.valueOf(2))) ||
                !is("-8", negative.xor(BigInteger.valueOf(3))) ||
                !is("4", negative.not())) {
            return -29;
        }
        if (!is("0", BigInteger.ZERO) ||
                !is("1", BigInteger.ONE) ||
                !is("2", BigInteger.TWO) ||
                !is("10", BigInteger.TEN) ||
                BigInteger.ONE != BigInteger.ONE) {
            return -30;
        }
        return 96;
    }
}
