package sun.security.provider;

// 生成补源（sun-compat）：sun.security.provider.PolicyFile（Policy 默认文件策略）。
// JBR 25 无此类；vendored Policy/ProtectionDomain 引用。OpenJDK 11 语义子集。
public class PolicyFile extends java.security.Policy {
    private final jdk.internal.misc.JavaSecurityAccess.ProtectionDomainCache cache =
        new jdk.internal.misc.JavaSecurityAccess.ProtectionDomainCache() {
        private final java.util.Map<Object, java.security.PermissionCollection> map =
            java.util.Collections.synchronizedMap(new java.util.WeakHashMap<>());

        @Override
        public void put(java.security.ProtectionDomain pd,
                        java.security.PermissionCollection pc) {
            map.put(pd, pc);
        }

        @Override
        public java.security.PermissionCollection get(java.security.ProtectionDomain pd) {
            return map.get(pd);
        }
    };

    public PolicyFile() {
    }

    public jdk.internal.misc.JavaSecurityAccess.ProtectionDomainCache getProtectionDomainCache() {
        return cache;
    }

    @Override
    public java.security.PermissionCollection getPermissions(
            java.security.CodeSource codesource) {
        return new java.security.Permissions();
    }

    @Override
    public java.security.PermissionCollection getPermissions(
            java.security.ProtectionDomain domain) {
        return new java.security.Permissions();
    }

    @Override
    public boolean implies(java.security.ProtectionDomain domain,
                           java.security.Permission permission) {
        return true;
    }

    @Override
    public void refresh() {
    }
}
