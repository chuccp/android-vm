package java.lang.invoke;

/**
 * d8 脱糖形状（java.lang.invoke 家族修复；docs §31.7 复类永远 lib）。
 *
 * guard_* 是 VarHandle accessor 的规范化派发形状：解释器
 * （core/interpreter.cc invoke-polymorphic）把调用点按 proto 形状转到
 * 对应 guard_*，guard_* 按字段读写语义转发到 VarHandle.access（mode 决定
 * 语义：get/set/getAndSet/getAndAdd/getAndBitwise 系/CAS/CAE 族）。
 *
 * 形状编码：guard_<坐标类型串>_<返回类型>——坐标类型串 = handle 之后各参的
 * basic type（Z/B/C/S 归 I，L=引用/数组接收者），返回类型保留 Z。
 * 形状相同即转发体相同（mode 于 AccessDescriptor 内区分语义），故同名覆盖
 * 多种 op。本文件由形状矩阵生成（75 个形状），勿手改。
 */
public final class VarHandleGuards {

    private VarHandleGuards() { }

    public static final double guard__D(VarHandle handle, VarHandle.AccessDescriptor ad) {
        return ((Number) handle.access(ad.mode)).doubleValue();
    }

    public static final float guard__F(VarHandle handle, VarHandle.AccessDescriptor ad) {
        return ((Number) handle.access(ad.mode)).floatValue();
    }

    public static final int guard__I(VarHandle handle, VarHandle.AccessDescriptor ad) {
        return ((Number) handle.access(ad.mode)).intValue();
    }

    public static final long guard__J(VarHandle handle, VarHandle.AccessDescriptor ad) {
        return ((Number) handle.access(ad.mode)).longValue();
    }

    public static final Object guard__L(VarHandle handle, VarHandle.AccessDescriptor ad) {
        return handle.access(ad.mode);
    }

    public static final boolean guard__Z(VarHandle handle, VarHandle.AccessDescriptor ad) {
        return ((Boolean) handle.access(ad.mode)).booleanValue();
    }

    public static final double guard_D_D(VarHandle handle, double arg0, VarHandle.AccessDescriptor ad) {
        return ((Number) handle.access(ad.mode, Double.valueOf(arg0))).doubleValue();
    }

    public static final void guard_D_V(VarHandle handle, double arg0, VarHandle.AccessDescriptor ad) {
        handle.access(ad.mode, Double.valueOf(arg0));
    }

    public static final float guard_F_F(VarHandle handle, float arg0, VarHandle.AccessDescriptor ad) {
        return ((Number) handle.access(ad.mode, Float.valueOf(arg0))).floatValue();
    }

    public static final void guard_F_V(VarHandle handle, float arg0, VarHandle.AccessDescriptor ad) {
        handle.access(ad.mode, Float.valueOf(arg0));
    }

    public static final int guard_I_I(VarHandle handle, int arg0, VarHandle.AccessDescriptor ad) {
        return ((Number) handle.access(ad.mode, Integer.valueOf(arg0))).intValue();
    }

    public static final void guard_I_V(VarHandle handle, int arg0, VarHandle.AccessDescriptor ad) {
        handle.access(ad.mode, Integer.valueOf(arg0));
    }

    public static final boolean guard_I_Z(VarHandle handle, int arg0, VarHandle.AccessDescriptor ad) {
        return ((Boolean) handle.access(ad.mode, Integer.valueOf(arg0))).booleanValue();
    }

    public static final long guard_J_J(VarHandle handle, long arg0, VarHandle.AccessDescriptor ad) {
        return ((Number) handle.access(ad.mode, Long.valueOf(arg0))).longValue();
    }

    public static final void guard_J_V(VarHandle handle, long arg0, VarHandle.AccessDescriptor ad) {
        handle.access(ad.mode, Long.valueOf(arg0));
    }

    public static final double guard_L_D(VarHandle handle, Object arg0, VarHandle.AccessDescriptor ad) {
        return ((Number) handle.access(ad.mode, arg0)).doubleValue();
    }

    public static final float guard_L_F(VarHandle handle, Object arg0, VarHandle.AccessDescriptor ad) {
        return ((Number) handle.access(ad.mode, arg0)).floatValue();
    }

    public static final int guard_L_I(VarHandle handle, Object arg0, VarHandle.AccessDescriptor ad) {
        return ((Number) handle.access(ad.mode, arg0)).intValue();
    }

    public static final long guard_L_J(VarHandle handle, Object arg0, VarHandle.AccessDescriptor ad) {
        return ((Number) handle.access(ad.mode, arg0)).longValue();
    }

    public static final Object guard_L_L(VarHandle handle, Object arg0, VarHandle.AccessDescriptor ad) {
        return handle.access(ad.mode, arg0);
    }

    public static final void guard_L_V(VarHandle handle, Object arg0, VarHandle.AccessDescriptor ad) {
        handle.access(ad.mode, arg0);
    }

    public static final boolean guard_L_Z(VarHandle handle, Object arg0, VarHandle.AccessDescriptor ad) {
        return ((Boolean) handle.access(ad.mode, arg0)).booleanValue();
    }

    public static final double guard_DD_D(VarHandle handle, double arg0, double arg1, VarHandle.AccessDescriptor ad) {
        return ((Number) handle.access(ad.mode, Double.valueOf(arg0), Double.valueOf(arg1))).doubleValue();
    }

    public static final boolean guard_DD_Z(VarHandle handle, double arg0, double arg1, VarHandle.AccessDescriptor ad) {
        return ((Boolean) handle.access(ad.mode, Double.valueOf(arg0), Double.valueOf(arg1))).booleanValue();
    }

    public static final float guard_FF_F(VarHandle handle, float arg0, float arg1, VarHandle.AccessDescriptor ad) {
        return ((Number) handle.access(ad.mode, Float.valueOf(arg0), Float.valueOf(arg1))).floatValue();
    }

    public static final boolean guard_FF_Z(VarHandle handle, float arg0, float arg1, VarHandle.AccessDescriptor ad) {
        return ((Boolean) handle.access(ad.mode, Float.valueOf(arg0), Float.valueOf(arg1))).booleanValue();
    }

    public static final int guard_II_I(VarHandle handle, int arg0, int arg1, VarHandle.AccessDescriptor ad) {
        return ((Number) handle.access(ad.mode, Integer.valueOf(arg0), Integer.valueOf(arg1))).intValue();
    }

    public static final boolean guard_II_Z(VarHandle handle, int arg0, int arg1, VarHandle.AccessDescriptor ad) {
        return ((Boolean) handle.access(ad.mode, Integer.valueOf(arg0), Integer.valueOf(arg1))).booleanValue();
    }

    public static final long guard_JJ_J(VarHandle handle, long arg0, long arg1, VarHandle.AccessDescriptor ad) {
        return ((Number) handle.access(ad.mode, Long.valueOf(arg0), Long.valueOf(arg1))).longValue();
    }

    public static final boolean guard_JJ_Z(VarHandle handle, long arg0, long arg1, VarHandle.AccessDescriptor ad) {
        return ((Boolean) handle.access(ad.mode, Long.valueOf(arg0), Long.valueOf(arg1))).booleanValue();
    }

    public static final double guard_LD_D(VarHandle handle, Object arg0, double arg1, VarHandle.AccessDescriptor ad) {
        return ((Number) handle.access(ad.mode, arg0, Double.valueOf(arg1))).doubleValue();
    }

    public static final void guard_LD_V(VarHandle handle, Object arg0, double arg1, VarHandle.AccessDescriptor ad) {
        handle.access(ad.mode, arg0, Double.valueOf(arg1));
    }

    public static final float guard_LF_F(VarHandle handle, Object arg0, float arg1, VarHandle.AccessDescriptor ad) {
        return ((Number) handle.access(ad.mode, arg0, Float.valueOf(arg1))).floatValue();
    }

    public static final void guard_LF_V(VarHandle handle, Object arg0, float arg1, VarHandle.AccessDescriptor ad) {
        handle.access(ad.mode, arg0, Float.valueOf(arg1));
    }

    public static final double guard_LI_D(VarHandle handle, Object arg0, int arg1, VarHandle.AccessDescriptor ad) {
        return ((Number) handle.access(ad.mode, arg0, Integer.valueOf(arg1))).doubleValue();
    }

    public static final float guard_LI_F(VarHandle handle, Object arg0, int arg1, VarHandle.AccessDescriptor ad) {
        return ((Number) handle.access(ad.mode, arg0, Integer.valueOf(arg1))).floatValue();
    }

    public static final int guard_LI_I(VarHandle handle, Object arg0, int arg1, VarHandle.AccessDescriptor ad) {
        return ((Number) handle.access(ad.mode, arg0, Integer.valueOf(arg1))).intValue();
    }

    public static final long guard_LI_J(VarHandle handle, Object arg0, int arg1, VarHandle.AccessDescriptor ad) {
        return ((Number) handle.access(ad.mode, arg0, Integer.valueOf(arg1))).longValue();
    }

    public static final Object guard_LI_L(VarHandle handle, Object arg0, int arg1, VarHandle.AccessDescriptor ad) {
        return handle.access(ad.mode, arg0, Integer.valueOf(arg1));
    }

    public static final void guard_LI_V(VarHandle handle, Object arg0, int arg1, VarHandle.AccessDescriptor ad) {
        handle.access(ad.mode, arg0, Integer.valueOf(arg1));
    }

    public static final boolean guard_LI_Z(VarHandle handle, Object arg0, int arg1, VarHandle.AccessDescriptor ad) {
        return ((Boolean) handle.access(ad.mode, arg0, Integer.valueOf(arg1))).booleanValue();
    }

    public static final long guard_LJ_J(VarHandle handle, Object arg0, long arg1, VarHandle.AccessDescriptor ad) {
        return ((Number) handle.access(ad.mode, arg0, Long.valueOf(arg1))).longValue();
    }

    public static final void guard_LJ_V(VarHandle handle, Object arg0, long arg1, VarHandle.AccessDescriptor ad) {
        handle.access(ad.mode, arg0, Long.valueOf(arg1));
    }

    public static final Object guard_LL_L(VarHandle handle, Object arg0, Object arg1, VarHandle.AccessDescriptor ad) {
        return handle.access(ad.mode, arg0, arg1);
    }

    public static final void guard_LL_V(VarHandle handle, Object arg0, Object arg1, VarHandle.AccessDescriptor ad) {
        handle.access(ad.mode, arg0, arg1);
    }

    public static final boolean guard_LL_Z(VarHandle handle, Object arg0, Object arg1, VarHandle.AccessDescriptor ad) {
        return ((Boolean) handle.access(ad.mode, arg0, arg1)).booleanValue();
    }

    public static final double guard_LDD_D(VarHandle handle, Object arg0, double arg1, double arg2, VarHandle.AccessDescriptor ad) {
        return ((Number) handle.access(ad.mode, arg0, Double.valueOf(arg1), Double.valueOf(arg2))).doubleValue();
    }

    public static final boolean guard_LDD_Z(VarHandle handle, Object arg0, double arg1, double arg2, VarHandle.AccessDescriptor ad) {
        return ((Boolean) handle.access(ad.mode, arg0, Double.valueOf(arg1), Double.valueOf(arg2))).booleanValue();
    }

    public static final float guard_LFF_F(VarHandle handle, Object arg0, float arg1, float arg2, VarHandle.AccessDescriptor ad) {
        return ((Number) handle.access(ad.mode, arg0, Float.valueOf(arg1), Float.valueOf(arg2))).floatValue();
    }

    public static final boolean guard_LFF_Z(VarHandle handle, Object arg0, float arg1, float arg2, VarHandle.AccessDescriptor ad) {
        return ((Boolean) handle.access(ad.mode, arg0, Float.valueOf(arg1), Float.valueOf(arg2))).booleanValue();
    }

    public static final double guard_LID_D(VarHandle handle, Object arg0, int arg1, double arg2, VarHandle.AccessDescriptor ad) {
        return ((Number) handle.access(ad.mode, arg0, Integer.valueOf(arg1), Double.valueOf(arg2))).doubleValue();
    }

    public static final void guard_LID_V(VarHandle handle, Object arg0, int arg1, double arg2, VarHandle.AccessDescriptor ad) {
        handle.access(ad.mode, arg0, Integer.valueOf(arg1), Double.valueOf(arg2));
    }

    public static final float guard_LIF_F(VarHandle handle, Object arg0, int arg1, float arg2, VarHandle.AccessDescriptor ad) {
        return ((Number) handle.access(ad.mode, arg0, Integer.valueOf(arg1), Float.valueOf(arg2))).floatValue();
    }

    public static final void guard_LIF_V(VarHandle handle, Object arg0, int arg1, float arg2, VarHandle.AccessDescriptor ad) {
        handle.access(ad.mode, arg0, Integer.valueOf(arg1), Float.valueOf(arg2));
    }

    public static final int guard_LII_I(VarHandle handle, Object arg0, int arg1, int arg2, VarHandle.AccessDescriptor ad) {
        return ((Number) handle.access(ad.mode, arg0, Integer.valueOf(arg1), Integer.valueOf(arg2))).intValue();
    }

    public static final void guard_LII_V(VarHandle handle, Object arg0, int arg1, int arg2, VarHandle.AccessDescriptor ad) {
        handle.access(ad.mode, arg0, Integer.valueOf(arg1), Integer.valueOf(arg2));
    }

    public static final boolean guard_LII_Z(VarHandle handle, Object arg0, int arg1, int arg2, VarHandle.AccessDescriptor ad) {
        return ((Boolean) handle.access(ad.mode, arg0, Integer.valueOf(arg1), Integer.valueOf(arg2))).booleanValue();
    }

    public static final long guard_LIJ_J(VarHandle handle, Object arg0, int arg1, long arg2, VarHandle.AccessDescriptor ad) {
        return ((Number) handle.access(ad.mode, arg0, Integer.valueOf(arg1), Long.valueOf(arg2))).longValue();
    }

    public static final void guard_LIJ_V(VarHandle handle, Object arg0, int arg1, long arg2, VarHandle.AccessDescriptor ad) {
        handle.access(ad.mode, arg0, Integer.valueOf(arg1), Long.valueOf(arg2));
    }

    public static final Object guard_LIL_L(VarHandle handle, Object arg0, int arg1, Object arg2, VarHandle.AccessDescriptor ad) {
        return handle.access(ad.mode, arg0, Integer.valueOf(arg1), arg2);
    }

    public static final void guard_LIL_V(VarHandle handle, Object arg0, int arg1, Object arg2, VarHandle.AccessDescriptor ad) {
        handle.access(ad.mode, arg0, Integer.valueOf(arg1), arg2);
    }

    public static final long guard_LJJ_J(VarHandle handle, Object arg0, long arg1, long arg2, VarHandle.AccessDescriptor ad) {
        return ((Number) handle.access(ad.mode, arg0, Long.valueOf(arg1), Long.valueOf(arg2))).longValue();
    }

    public static final boolean guard_LJJ_Z(VarHandle handle, Object arg0, long arg1, long arg2, VarHandle.AccessDescriptor ad) {
        return ((Boolean) handle.access(ad.mode, arg0, Long.valueOf(arg1), Long.valueOf(arg2))).booleanValue();
    }

    public static final Object guard_LLL_L(VarHandle handle, Object arg0, Object arg1, Object arg2, VarHandle.AccessDescriptor ad) {
        return handle.access(ad.mode, arg0, arg1, arg2);
    }

    public static final boolean guard_LLL_Z(VarHandle handle, Object arg0, Object arg1, Object arg2, VarHandle.AccessDescriptor ad) {
        return ((Boolean) handle.access(ad.mode, arg0, arg1, arg2)).booleanValue();
    }

    public static final double guard_LIDD_D(VarHandle handle, Object arg0, int arg1, double arg2, double arg3, VarHandle.AccessDescriptor ad) {
        return ((Number) handle.access(ad.mode, arg0, Integer.valueOf(arg1), Double.valueOf(arg2), Double.valueOf(arg3))).doubleValue();
    }

    public static final boolean guard_LIDD_Z(VarHandle handle, Object arg0, int arg1, double arg2, double arg3, VarHandle.AccessDescriptor ad) {
        return ((Boolean) handle.access(ad.mode, arg0, Integer.valueOf(arg1), Double.valueOf(arg2), Double.valueOf(arg3))).booleanValue();
    }

    public static final float guard_LIFF_F(VarHandle handle, Object arg0, int arg1, float arg2, float arg3, VarHandle.AccessDescriptor ad) {
        return ((Number) handle.access(ad.mode, arg0, Integer.valueOf(arg1), Float.valueOf(arg2), Float.valueOf(arg3))).floatValue();
    }

    public static final boolean guard_LIFF_Z(VarHandle handle, Object arg0, int arg1, float arg2, float arg3, VarHandle.AccessDescriptor ad) {
        return ((Boolean) handle.access(ad.mode, arg0, Integer.valueOf(arg1), Float.valueOf(arg2), Float.valueOf(arg3))).booleanValue();
    }

    public static final int guard_LIII_I(VarHandle handle, Object arg0, int arg1, int arg2, int arg3, VarHandle.AccessDescriptor ad) {
        return ((Number) handle.access(ad.mode, arg0, Integer.valueOf(arg1), Integer.valueOf(arg2), Integer.valueOf(arg3))).intValue();
    }

    public static final boolean guard_LIII_Z(VarHandle handle, Object arg0, int arg1, int arg2, int arg3, VarHandle.AccessDescriptor ad) {
        return ((Boolean) handle.access(ad.mode, arg0, Integer.valueOf(arg1), Integer.valueOf(arg2), Integer.valueOf(arg3))).booleanValue();
    }

    public static final long guard_LIJJ_J(VarHandle handle, Object arg0, int arg1, long arg2, long arg3, VarHandle.AccessDescriptor ad) {
        return ((Number) handle.access(ad.mode, arg0, Integer.valueOf(arg1), Long.valueOf(arg2), Long.valueOf(arg3))).longValue();
    }

    public static final boolean guard_LIJJ_Z(VarHandle handle, Object arg0, int arg1, long arg2, long arg3, VarHandle.AccessDescriptor ad) {
        return ((Boolean) handle.access(ad.mode, arg0, Integer.valueOf(arg1), Long.valueOf(arg2), Long.valueOf(arg3))).booleanValue();
    }

    public static final Object guard_LILL_L(VarHandle handle, Object arg0, int arg1, Object arg2, Object arg3, VarHandle.AccessDescriptor ad) {
        return handle.access(ad.mode, arg0, Integer.valueOf(arg1), arg2, arg3);
    }

    public static final boolean guard_LILL_Z(VarHandle handle, Object arg0, int arg1, Object arg2, Object arg3, VarHandle.AccessDescriptor ad) {
        return ((Boolean) handle.access(ad.mode, arg0, Integer.valueOf(arg1), arg2, arg3)).booleanValue();
    }

}
