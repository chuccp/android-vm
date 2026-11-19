package tptest;

import com.fasterxml.jackson.core.util.DefaultPrettyPrinter;
import com.fasterxml.jackson.core.util.Separators;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.gson.Gson;
import java.util.Map;
import org.apache.commons.lang3.ArrayUtils;

/**
 * 三方库 conformance 自检（覆盖面参考启发自 v1 NativeVmThirdPartyCoroutineTest.kt
 * thirdPartyLibrariesFlow/jacksonInitializationFlow 面 + RuntimeThirdPartyCoroutine
 * Fixture；判据 = 各库文档语义在 Android/Dalvik 上的行为）。
 * 可跑集裁剪：Jackson(tree/pretty-print)/Gson/commons-lang3/okio(hash)；
 * Netty/RxJava/fastjson/kotlinx.coroutines 结构化并发面后置（见脚本注记）。
 * 每 check* 返回 "name:ok" 或 "name:fail:<原因>"。
 */
public final class ThirdPartyChecks {
    private ThirdPartyChecks() {}

    /** 失败原因附 cause 链（3 层）便于缺口归因。 */
    private static String reason(Throwable failure) {
        StringBuilder sb = new StringBuilder();
        Throwable current = failure;
        for (int depth = 0; depth < 3 && current != null; depth++) {
            if (depth > 0) sb.append("<-");
            sb.append(current.getClass().getSimpleName());
            if (current.getMessage() != null) {
                sb.append('(').append(current.getMessage()).append(')');
            }
            current = current.getCause();
        }
        return sb.toString();
    }

    public static String checkJacksonTree() {
        try {
            ObjectMapper mapper = new ObjectMapper();
            com.fasterxml.jackson.databind.JsonNode parsed = mapper.readTree(
                    "{\"displayName\":\"vm\",\"count\":3,\"ignored\":true}");
            if (!"vm".equals(parsed.path("displayName").asText())) {
                return "jacksonTree:fail:displayName";
            }
            if (parsed.path("count").asInt() != 3) {
                return "jacksonTree:fail:count";
            }
            String json = parsed.toString();
            return json.contains("\"count\":3") ? "jacksonTree:ok" : "jacksonTree:fail:ser";
        } catch (Throwable failure) {
            return "jacksonTree:fail:" + reason(failure);
        }
    }

    public static String checkJacksonPretty() {
        try {
            Separators.Spacing spacing = Separators.Spacing.BOTH;
            if (!" ".equals(spacing.spacesBefore())) return "jacksonPretty:fail:before";
            if (!" ".equals(spacing.spacesAfter())) return "jacksonPretty:fail:after";
            if (!" : ".equals(spacing.apply(':'))) return "jacksonPretty:fail:apply";

            Separators separators = new Separators();
            if (!" ".equals(separators.getRootSeparator())) {
                return "jacksonPretty:fail:root";
            }
            if (separators.getObjectFieldValueSpacing() != spacing) {
                return "jacksonPretty:fail:fieldSpacing";
            }
            new DefaultPrettyPrinter(separators);
            return "jacksonPretty:ok";
        } catch (Throwable failure) {
            return "jacksonPretty:fail:" + reason(failure);
        }
    }

    public static String checkGson() {
        try {
            Gson gson = new Gson();
            Map<?, ?> decoded = gson.fromJson(
                    "{\"displayName\":\"vm\",\"count\":3}", Map.class);
            if (!"vm".equals(decoded.get("displayName"))) return "gson:fail:decode";
            String encoded = gson.toJson(decoded);
            Map<?, ?> roundtrip = gson.fromJson(encoded, Map.class);
            return "vm".equals(roundtrip.get("displayName"))
                    ? "gson:ok" : "gson:fail:roundtrip";
        } catch (Throwable failure) {
            return "gson:fail:" + reason(failure);
        }
    }

    public static String checkCommonsLang() {
        try {
            if (ArrayUtils.indexOf(new int[] {4, 8, 15, 16}, 15) != 2) {
                return "commonsLang:fail:indexOf";
            }
            if (!ArrayUtils.isNotEmpty(new int[] {1})) return "commonsLang:fail:isNotEmpty";
            if (ArrayUtils.isNotEmpty(new int[0])) return "commonsLang:fail:empty";
            return "commonsLang:ok";
        } catch (Throwable failure) {
            return "commonsLang:fail:" + reason(failure);
        }
    }

    public static String checkOkioHash() {
        try {
            okio.Buffer buffer = new okio.Buffer();
            buffer.writeUtf8("vm-coroutines");
            String hex = buffer.sha256().hex();
            return "138057622124c210770d0936e8f6bd1bc3e161b29b995bd3a8fcb161a4530685"
                    .equals(hex)
                    ? "okioHash:ok" : "okioHash:fail:" + hex;
        } catch (Throwable failure) {
            return "okioHash:fail:" + reason(failure);
        }
    }
}
