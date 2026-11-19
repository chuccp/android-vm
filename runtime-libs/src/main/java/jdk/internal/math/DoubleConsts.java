/*
 * Copyright (c) 1998, 2023, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 */

package jdk.internal.math;

/**
 * This class contains additional constants documenting limitations of the
 * {@code double} type, as well as some useful minimum and maximum values.
 *
 * @since 1.5
 */
public final class DoubleConsts {
    private DoubleConsts() {}

    /**
     * The number of logical bits in the significand of a
     * {@code double} number, including the implicit bit.
     */
    public static final int SIGNIFICAND_WIDTH   = 53;

    /**
     * The exponent the all-ones {@code double} has.
     */
    public static final int EXP_BIAS            = 1023;

    /**
     * Mask for extracting the exponent of a {@code double}.
     */
    public static final long EXP_BIT_MASK       = 0x7FF0000000000000L;

    /**
     * Mask for extracting the significand of a {@code double}.
     */
    public static final long SIGNIF_BIT_MASK    = 0x000FFFFFFFFFFFFFL;

    /**
     * Mask for extracting the sign of a {@code double}.
     */
    public static final long SIGN_BIT_MASK      = 0x8000000000000000L;

    /**
     * The smallest positive normal value of type {@code double}.
     */
    public static final double MIN_NORMAL       = 0x1.0p-1022;

    /**
     * The smallest positive value of type {@code double}.
     */
    public static final double MIN_VALUE        = 0x0.0000000000001P-1022;

    /**
     * The largest positive value of type {@code double}.
     */
    public static final double MAX_VALUE        = 0x1.fffffffffffffP+1023;
}
