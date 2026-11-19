package java.lang;

class FdLibm {

    private static final double INFINITY = Double.POSITIVE_INFINITY;

    private FdLibm() {
        throw new UnsupportedOperationException("No FdLibm instances for you.");
    }

    private static int __LO(double x) {
        long transducer = Double.doubleToRawLongBits(x);
        return (int)transducer;
    }

    private static double __LO(double x, int low) {
        long transX = Double.doubleToRawLongBits(x);
        return Double.longBitsToDouble((transX & 0xFFFF_FFFF_0000_0000L) |
                                       (low    & 0x0000_0000_FFFF_FFFFL));
    }

    private static int __HI(double x) {
        long transducer = Double.doubleToRawLongBits(x);
        return (int)(transducer >> 32);
    }

    private static double __HI(double x, int high) {
        long transX = Double.doubleToRawLongBits(x);
        return Double.longBitsToDouble((transX & 0x0000_0000_FFFF_FFFFL) |
                                       ( ((long)high)) << 32 );
    }

    public static class Cbrt {

        private static final int B1 = 715094163;
        private static final int B2 = 696219795;

        private static final double C =  0x1.15f15f15f15f1p-1;
        private static final double D = -0x1.691de2532c834p-1;
        private static final double E =  0x1.6a0ea0ea0ea0fp0;
        private static final double F =  0x1.9b6db6db6db6ep0;
        private static final double G =  0x1.6db6db6db6db7p-2;

        private Cbrt() {
            throw new UnsupportedOperationException();
        }

        public static strictfp double compute(double x) {
            double  t = 0.0;
            double sign;

            if (x == 0.0 || !Double.isFinite(x))
                return x;

            sign = (x < 0.0) ? -1.0:  1.0;

            x = Math.abs(x);

            if (x < 0x1.0p-1022) {
                t = 0x1.0p54;
                t *= x;
                t = __HI(t, __HI(t)/3 + B2);
            } else {
                int hx = __HI(x);
                t = __HI(t, hx/3 + B1);
            }

            double  r, s, w;
            r = t * t/x;
            s = C + r*t;
            t *= G + F/(s + E + D/s);

            t = __LO(t, 0);
            t = __HI(t, __HI(t) + 0x00000001);

            s = t * t;
            r = x / s;
            w = t + t;
            r = (r - t)/(w + r);
            t = t + t*r;

            return sign * t;
        }
    }

    public static class Hypot {
        public static final double TWO_MINUS_600 = 0x1.0p-600;
        public static final double TWO_PLUS_600  = 0x1.0p+600;

        private Hypot() {
            throw new UnsupportedOperationException();
        }

        public static strictfp double compute(double x, double y) {
            double a = Math.abs(x);
            double b = Math.abs(y);

            if (!Double.isFinite(a) || !Double.isFinite(b)) {
                if (a == INFINITY || b == INFINITY)
                    return INFINITY;
                else
                    return a + b;
            }

            if (b > a) {
                double tmp = a;
                a = b;
                b = tmp;
            }
            assert a >= b;

            int ha = __HI(a);
            int hb = __HI(b);

            if ((ha - hb) > 0x3c00000) {
                return a + b;
            }

            int k = 0;
            if (a > 0x1.00000_ffff_ffffp500) {

                ha -= 0x25800000;
                hb -= 0x25800000;
                a = a * TWO_MINUS_600;
                b = b * TWO_MINUS_600;
                k += 600;
            }
            double t1, t2;
            if (b < 0x1.0p-500) {
                if (b < Double.MIN_NORMAL) {
                    if (b == 0.0)
                        return a;
                    t1 = 0x1.0p1022;
                    b *= t1;
                    a *= t1;
                    k -= 1022;
                } else {
                    ha += 0x25800000;
                    hb += 0x25800000;
                    a = a * TWO_PLUS_600;
                    b = b * TWO_PLUS_600;
                    k -= 600;
                }
            }

            double w = a - b;
            if (w > b) {
                t1 = 0;
                t1 = __HI(t1, ha);
                t2 = a - t1;
                w  = Math.sqrt(t1*t1 - (b*(-b) - t2 * (a + t1)));
            } else {
                double y1, y2;
                a  = a + a;
                y1 = 0;
                y1 = __HI(y1, hb);
                y2 = b - y1;
                t1 = 0;
                t1 = __HI(t1, ha + 0x00100000);
                t2 = a - t1;
                w  = Math.sqrt(t1*y1 - (w*(-w) - (t1*y2 + t2*b)));
            }
            if (k != 0) {
                return Math.powerOfTwoD(k) * w;
            } else
                return w;
        }
    }

    public static class Pow {
        private Pow() {
            throw new UnsupportedOperationException();
        }

        public static strictfp double compute(final double x, final double y) {
            double z;
            double r, s, t, u, v, w;
            int i, j, k, n;

            if (y == 0.0)
                return 1.0;

            if (Double.isNaN(x) || Double.isNaN(y))
                return x + y;

            final double y_abs = Math.abs(y);
            double x_abs   = Math.abs(x);

            if (y == 2.0) {
                return x * x;
            } else if (y == 0.5) {
                if (x >= -Double.MAX_VALUE)
                    return Math.sqrt(x + 0.0);
            } else if (y_abs == 1.0) {
                return (y == 1.0) ? x : 1.0 / x;
            } else if (y_abs == INFINITY) {
                if (x_abs == 1.0)
                    return  y - y;
                else if (x_abs > 1.0)
                    return (y >= 0) ? y : 0.0;
                else
                    return (y < 0) ? -y : 0.0;
            }

            final int hx = __HI(x);
            int ix = hx & 0x7fffffff;

            int y_is_int  = 0;
            if (hx < 0) {
                if (y_abs >= 0x1.0p53)
                    y_is_int = 2;
                else if (y_abs >= 1.0) {
                    long y_abs_as_long = (long) y_abs;
                    if ( ((double) y_abs_as_long) == y_abs) {
                        y_is_int = 2 -  (int)(y_abs_as_long & 0x1L);
                    }
                }
            }

            if (x_abs == 0.0 ||
                x_abs == INFINITY ||
                x_abs == 1.0) {
                z = x_abs;
                if (y < 0.0)
                    z = 1.0/z;
                if (hx < 0) {
                    if (((ix - 0x3ff00000) | y_is_int) == 0) {
                        z = (z-z)/(z-z);
                    } else if (y_is_int == 1)
                        z = -1.0 * z;
                }
                return z;
            }

            n = (hx >> 31) + 1;

            if ((n | y_is_int) == 0)
                return (x-x)/(x-x);

            s = 1.0;
            if ( (n | (y_is_int - 1)) == 0)
                s = -1.0;

            double p_h, p_l, t1, t2;

            if (y_abs > 0x1.00000_ffff_ffffp31) {
                final double INV_LN2   =  0x1.7154_7652_b82fep0;
                final double INV_LN2_H =  0x1.715476p0;
                final double INV_LN2_L =  0x1.4ae0_bf85_ddf44p-26;

                if (x_abs < 0x1.fffff_0000_0000p-1)
                    return (y < 0.0) ? s * INFINITY : s * 0.0;
                if (x_abs > 0x1.00000_ffff_ffffp0)
                    return (y > 0.0) ? s * INFINITY : s * 0.0;

                t = x_abs - 1.0;
                w = (t * t) * (0.5 - t * (0.3333333333333333333333 - t * 0.25));
                u = INV_LN2_H * t;
                v =  t * INV_LN2_L - w * INV_LN2;
                t1 = u + v;
                t1 =__LO(t1, 0);
                t2 = v - (t1 - u);
            } else {
                final double CP      =  0x1.ec70_9dc3_a03fdp-1;
                final double CP_H    =  0x1.ec709ep-1;
                final double CP_L    = -0x1.e2fe_0145_b01f5p-28;

                double z_h, z_l, ss, s2, s_h, s_l, t_h, t_l;
                n = 0;

                if (ix < 0x00100000) {
                    x_abs *= 0x1.0p53;
                    n -= 53;
                    ix = __HI(x_abs);
                }
                n  += ((ix) >> 20) - 0x3ff;
                j  = ix & 0x000fffff;

                ix = j | 0x3ff00000;
                if (j <= 0x3988E)
                    k = 0;
                else if (j < 0xBB67A)
                    k = 1;
                else {
                    k = 0;
                    n += 1;
                    ix -= 0x00100000;
                }
                x_abs = __HI(x_abs, ix);

                final double BP[]    = {1.0,
                                       1.5};
                final double DP_H[]  = {0.0,
                                        0x1.2b80_34p-1};
                final double DP_L[]  = {0.0,
                                        0x1.cfde_b43c_fd006p-27};

                final double L1      =  0x1.3333_3333_33303p-1;
                final double L2      =  0x1.b6db_6db6_fabffp-2;
                final double L3      =  0x1.5555_5518_f264dp-2;
                final double L4      =  0x1.1746_0a91_d4101p-2;
                final double L5      =  0x1.d864_a93c_9db65p-3;
                final double L6      =  0x1.a7e2_84a4_54eefp-3;
                u = x_abs - BP[k];
                v = 1.0 / (x_abs + BP[k]);
                ss = u * v;
                s_h = ss;
                s_h = __LO(s_h, 0);

                t_h = 0.0;
                t_h = __HI(t_h, ((ix >> 1) | 0x20000000) + 0x00080000 + (k << 18) );
                t_l = x_abs - (t_h - BP[k]);
                s_l = v * ((u - s_h * t_h) - s_h * t_l);

                s2 = ss * ss;
                r = s2 * s2* (L1 + s2 * (L2 + s2 * (L3 + s2 * (L4 + s2 * (L5 + s2 * L6)))));
                r += s_l * (s_h + ss);
                s2  = s_h * s_h;
                t_h = 3.0 + s2 + r;
                t_h = __LO(t_h, 0);
                t_l = r - ((t_h - 3.0) - s2);

                u = s_h * t_h;
                v = s_l * t_h + t_l * ss;

                p_h = u + v;
                p_h = __LO(p_h, 0);
                p_l = v - (p_h - u);
                z_h = CP_H * p_h;
                z_l = CP_L * p_h + p_l * CP + DP_L[k];

                t = (double)n;
                t1 = (((z_h + z_l) + DP_H[k]) + t);
                t1 = __LO(t1, 0);
                t2 = z_l - (((t1 - t) - DP_H[k]) - z_h);
            }

            double y1  = y;
            y1 = __LO(y1, 0);
            p_l = (y - y1) * t1 + y * t2;
            p_h = y1 * t1;
            z = p_l + p_h;
            j = __HI(z);
            i = __LO(z);
            if (j >= 0x40900000) {
                if (((j - 0x40900000) | i)!=0)
                    return s * INFINITY;
                else {
                    final double OVT     =  8.0085662595372944372e-0017;
                    if (p_l + OVT > z - p_h)
                        return s * INFINITY;
                }
            } else if ((j & 0x7fffffff) >= 0x4090cc00 ) {
                if (((j - 0xc090cc00) | i)!=0)
                    return s * 0.0;
                else {
                    if (p_l <= z - p_h)
                        return s * 0.0;
                }
            }

            final double P1      =  0x1.5555_5555_5553ep-3;
            final double P2      = -0x1.6c16_c16b_ebd93p-9;
            final double P3      =  0x1.1566_aaf2_5de2cp-14;
            final double P4      = -0x1.bbd4_1c5d_26bf1p-20;
            final double P5      =  0x1.6376_972b_ea4d0p-25;
            final double LG2     =  0x1.62e4_2fef_a39efp-1;
            final double LG2_H   =  0x1.62e43p-1;
            final double LG2_L   = -0x1.05c6_10ca_86c39p-29;
            i = j & 0x7fffffff;
            k = (i >> 20) - 0x3ff;
            n = 0;
            if (i > 0x3fe00000) {
                n = j + (0x00100000 >> (k + 1));
                k = ((n & 0x7fffffff) >> 20) - 0x3ff;
                t = 0.0;
                t = __HI(t, (n & ~(0x000fffff >> k)) );
                n = ((n & 0x000fffff) | 0x00100000) >> (20 - k);
                if (j < 0)
                    n = -n;
                p_h -= t;
            }
            t = p_l + p_h;
            t = __LO(t, 0);
            u = t * LG2_H;
            v = (p_l - (t - p_h)) * LG2 + t * LG2_L;
            z = u + v;
            w = v - (z - u);
            t  = z * z;
            t1  = z - t * (P1 + t * (P2 + t * (P3 + t * (P4 + t * P5))));
            r  = (z * t1)/(t1 - 2.0) - (w + z * w);
            z  = 1.0 - (r - z);
            j  = __HI(z);
            j += (n << 20);
            if ((j >> 20) <= 0)
                z = Math.scalb(z, n);
            else {
                int z_hi = __HI(z);
                z_hi += (n << 20);
                z = __HI(z, z_hi);
            }
            return s * z;
        }
    }

    static class Exp {
        private static final double one     = 1.0;
        private static final double[] half = {0.5, -0.5,};
        private static final double huge    = 1.0e+300;
        private static final double twom1000=     0x1.0p-1000;
        private static final double o_threshold=  0x1.62e42fefa39efp9;
        private static final double u_threshold= -0x1.74910d52d3051p9;
        private static final double[] ln2HI   ={  0x1.62e42feep-1,
                                                 -0x1.62e42feep-1};
        private static final double[] ln2LO   ={  0x1.a39ef35793c76p-33,
                                                 -0x1.a39ef35793c76p-33};
        private static final double invln2 =      0x1.71547652b82fep0;

        private static final double P1   =  0x1.555555555553ep-3;
        private static final double P2   = -0x1.6c16c16bebd93p-9;
        private static final double P3   =  0x1.1566aaf25de2cp-14;
        private static final double P4   = -0x1.bbd41c5d26bf1p-20;
        private static final double P5   =  0x1.6376972bea4d0p-25;

        private Exp() {
            throw new UnsupportedOperationException();
        }

        public static strictfp double compute(double x) {
            double y;
            double hi = 0.0;
            double lo = 0.0;
            double c;
            double t;
            int k = 0;
            int xsb;
             int hx;

            hx  = __HI(x);
            xsb = (hx >> 31) & 1;
            hx &= 0x7fffffff;

            if (hx >= 0x40862E42) {
                if (hx >= 0x7ff00000) {
                    if (((hx & 0xfffff) | __LO(x)) != 0)
                        return x + x;
                    else
                        return (xsb == 0) ? x : 0.0;
                }
                if (x > o_threshold)
                    return huge * huge;
                if (x < u_threshold)
                    return twom1000 * twom1000;
            }

            if (hx > 0x3fd62e42) {
                if(hx < 0x3FF0A2B2) {
                    hi = x - ln2HI[xsb];
                    lo=ln2LO[xsb];
                    k = 1 - xsb - xsb;
                } else {
                    k  = (int)(invln2 * x + half[xsb]);
                    t  = k;
                    hi = x - t*ln2HI[0];
                    lo = t*ln2LO[0];
                }
                x  = hi - lo;
            } else if (hx < 0x3e300000)  {
                if (huge + x > one)
                    return one + x;
            } else {
                k = 0;
            }

            t  = x * x;
            c  = x - t*(P1 + t*(P2 + t*(P3 + t*(P4 + t*P5))));
            if (k == 0)
                return one - ((x*c)/(c - 2.0) - x);
            else
                y = one - ((lo - (x*c)/(2.0 - c)) - hi);

            if(k >= -1021) {
                y = __HI(y, __HI(y) + (k << 20));
                return y;
            } else {
                y = __HI(y, __HI(y) + ((k + 1000) << 20));
                return y * twom1000;
            }
        }
    }
}
