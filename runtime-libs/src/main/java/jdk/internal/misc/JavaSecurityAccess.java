package jdk.internal.misc;

// 生成补源（sun-compat）：JavaSecurityAccess（ProtectionDomain 权限域接线，
// 签名对齐 java.security.ProtectionDomain.JavaSecurityAccessImpl）。
public interface JavaSecurityAccess {
    interface ProtectionDomainCache {
        void put(java.security.ProtectionDomain pd,
                 java.security.PermissionCollection pc);
        java.security.PermissionCollection get(java.security.ProtectionDomain pd);
    }

    java.security.ProtectionDomain[] getProtectDomains(
            java.security.AccessControlContext context);

    ProtectionDomainCache getProtectionDomainCache();

    <T> T doIntersectionPrivilege(java.security.PrivilegedAction<T> action,
                                  java.security.AccessControlContext context);
    <T> T doIntersectionPrivilege(java.security.PrivilegedAction<T> action,
                                  java.security.AccessControlContext context,
                                  java.security.AccessControlContext combined);
}
