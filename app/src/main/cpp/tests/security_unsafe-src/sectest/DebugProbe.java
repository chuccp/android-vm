package sectest;

import sun.security.util.Debug;

/**
 * Debug 面探针（首个探针：<clinit> 读 java.security.debug 系统属性，必须在
 * 任何 Debug 触碰前置属性）。覆盖：isOn 配置匹配（精确/x= 前缀形/all）、
 * getInstance null 语义、pdebug 面（provider/Provider + skipDebug 公式）、
 * println System.err 输出（harness 截获 stderr 断言 "Provider: ..." 行）、
 * toHexString。输出行格式 "debug.*: OK"/"debug.*: FAIL ..."。
 */
public class DebugProbe {

    public static String run() {
        StringBuilder sb = new StringBuilder();
        // 1) 属性先置、后触碰 Debug（<clinit> 捕获 args）
        System.setProperty("java.security.debug", "provider,engine=");
        try {
            if (Debug.isOn("provider") && Debug.isOn("engine=") && !Debug.isOn("securerandom")
                    && !Debug.isOn("all")) {
                sb.append("debug-isOn: OK\n");
            } else {
                sb.append("debug-isOn: FAIL matching\n");
            }
        } catch (Throwable t) {
            sb.append("debug-isOn: FAIL ").append(t).append('\n');
        }
        try {
            Debug off = Debug.getInstance("securerandom");
            Debug on = Debug.getInstance("provider", "Provider");
            if (off == null && on != null) {
                sb.append("debug-getInstance: OK\n");
            } else {
                sb.append("debug-getInstance: FAIL off=").append(off).append(" on=").append(on)
                        .append('\n');
            }
        } catch (Throwable t) {
            sb.append("debug-getInstance: FAIL ").append(t).append('\n');
        }
        try {
            // pdebug 面（SecureRandom.<clinit> 同款公式）+ System.err 输出语义
            Debug pdebug = Debug.getInstance("provider", "Provider");
            boolean skipDebug = Debug.isOn("engine=") && !Debug.isOn("securerandom");
            if (pdebug != null && skipDebug) {
                pdebug.println("hello-debug-line");
                pdebug.println();
                sb.append("debug-pdebug: OK\n");
            } else {
                sb.append("debug-pdebug: FAIL pdebug=").append(pdebug).append(" skipDebug=")
                        .append(skipDebug).append('\n');
            }
        } catch (Throwable t) {
            sb.append("debug-pdebug: FAIL ").append(t).append('\n');
        }
        try {
            String hex = Debug.toHexString(new byte[] {0x0a, (byte) 0xff});
            if ("0aff".equals(hex)) {
                sb.append("debug-toHexString: OK\n");
            } else {
                sb.append("debug-toHexString: FAIL ").append(hex).append('\n');
            }
        } catch (Throwable t) {
            sb.append("debug-toHexString: FAIL ").append(t).append('\n');
        }
        return sb.toString();
    }
}
