package android.content.res;

import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.io.InputStream;

/**
 * 资产管理器（AOSP android.content.res.AssetManager 骨架；§31.7 C 档 lib）。
 * 资产读取经 VM 资源缝（session zip）由 Resources 路径承担；本骨架保持
 * 引用面可解析（open/list 后置到资源缝批）。
 */
public final class AssetManager {
    /**
     * OkHttp PublicSuffixDatabase 的空后缀表载荷：**原始 gzip 字节**（该库在
     * readTheList 内自行套 GZIPInputStream 解压——open() 不能再解压一层）。
     * 解压后为两个大端 int 长度（均为 0），即「公共后缀表为空、例外表为空」。
     *
     * 为什么不能直接抛 IOException：该库的 readTheListUninterruptibly 捕获
     * IOException 后既不置 publicSuffixListBytes 也不 countDown 读完闩；此后
     * findMatchingRule 会抛 IllegalStateException("Unable to load
     * okhttpsuffixes.bin resource ...")，且并发网络线程会在 readCompleteLatch
     * 上永久等待。返回合法空表让链路按「无后缀规则」降级（binarySearchBytes
     * 对空数组安全返回 null，findMatchingRule 落到 PREVAILING_RULE）。
     */
    private static final byte[] EMPTY_SUFFIX_LIST_GZIP = {
        (byte) 0x1f, (byte) 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x02, (byte) 0xff, 0x63, 0x60, (byte) 0x80, 0x00, 0x00,
        0x69, (byte) 0xdf, 0x22, 0x65, 0x08, 0x00, 0x00, 0x00
    };

    AssetManager() {
    }

    public String[] list(String path) throws IOException {
        return new String[0];
    }

    public InputStream open(String fileName) throws IOException {
        return open(fileName, 0);
    }

    public InputStream open(String fileName, int accessMode) throws IOException {
        if ("okhttpsuffixes.bin".equals(fileName)) {
            // 原始 gzip 字节；解压由调用方（readTheList 的 GZIPInputStream）完成。
            return new ByteArrayInputStream(EMPTY_SUFFIX_LIST_GZIP);
        }
        // 真实资产：APK zip 的 assets/<name>（VM 资产缝；Tabbar_*.json /
        // appconfig / 证书等）。查无 = IOException（AOSP FileNotFoundException 同族）。
        byte[] data = VmAssets.open(fileName);
        if (data != null) {
            return new ByteArrayInputStream(data);
        }
        throw new IOException("AssetManager.open: asset not found: " + fileName);
    }

    public void close() {
    }
}
