package android.view;

import android.content.Context;

/**
 * VM 平台 LayoutInflater（AOSP PhoneLayoutInflater 等价物）：inflate 走 VM 资源
 * 布局引擎（core/framework_inflate.cc，AXML -> guest View 树），工厂链（Factory/
 * Factory2）由 VM 侧后续接线；本类只做 Java 可构造面 + 委派钩子。
 */
public class VmLayoutInflater extends LayoutInflater {

    public VmLayoutInflater(Context context) {
        super(context);
    }

    protected VmLayoutInflater(LayoutInflater original, Context newContext) {
        super(original, newContext);
    }

    @Override
    public LayoutInflater cloneInContext(Context newContext) {
        return new VmLayoutInflater(this, newContext);
    }

    @Override
    public View inflate(int resource, ViewGroup root, boolean attachToRoot) {
        return VmLayoutHost.inflate(this, resource, root, attachToRoot);
    }
}
