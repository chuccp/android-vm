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
 * {@code float} type, as well as some useful minimum and maximum values.
 *
 * @since 1.5
 */
public final class FloatConsts {
    private FloatConsts() {}

    /**
     * The number of logical bits in the significand of a
     * {@code float} number, including the implicit bit.
     */
    public static final int SIGNIFICAND_WIDTH   = 24;

    /**
     * The exponent the all-ones {@code float} has.
     */
    public static final int EXP_BIAS            = 127;

    /**
     * Mask for extracting the exponent of a {@code float}.
     */
    public static final int EXP_BIT_MASK        = 0x7F800000;

    /**
     * Mask for extracting the significand of a {@code float}.
     */
    public static final int SIGNIF_BIT_MASK     = 0x007FFFFF;

    /**
     * Mask for extracting the sign of a {@code float}.
     */
    public static final int SIGN_BIT_MASK       = 0x80000000;

    /**
     * The smallest positive normal value of type {@code float}.
     */
    public static final float MIN_NORMAL        = 0x1.0p-126f;

    /**
     * The smallest positive value of type {@code float}.
     */
    public static final float MIN_VALUE         = 0x0.000002p-126f;

    /**
     * The largest positive value of type {@code float}.
     */
    public static final float MAX_VALUE         = 0x1.fffffep+127f;
}
