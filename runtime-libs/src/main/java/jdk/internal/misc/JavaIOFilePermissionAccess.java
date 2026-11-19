package jdk.internal.misc;

public interface JavaIOFilePermissionAccess {
    java.io.FilePermission newPermPlusAltPath(java.io.FilePermission input);
    java.io.FilePermission newPermUsingAltPath(java.io.FilePermission input);
}
