package jdtest;

// SecNeo（梆梆）解密回调形态压力 fixture（tools/jni_deep_tests.sh）。
// 排查方针（用户定性）：应用不会合法深递归——深栈/不终止循环 = 语义偏差实证。
// 用例按【带终止条件的正常调用循环】构造（正确语义应有限圈结束）：
//   1) 对象链终止循环：native 循环 CallObjectMethodV(next) 走到 null 必须停
//      （返回值语义：该 null 却非空 = 死循环伪递归实证）
//   2) 字符串语义回环：GetStringLength/GetStringUTFLength/GetStringUTFChars
//      内容 + NewStringUTF 回环，与 Android MUTF-8 语义逐字节对齐（空串/中文/
//      内嵌 NUL/增补平面）——内容偏差让壳判"还有数据" = 终止失效实证
//   3) 接口派发终止循环：接口 methodID 派发（FindInterfaceImpl 回环怀疑面）
//   4) local ref churn + 代回绕（悬挂 jobject 误判 = 纯语义，与栈无关）
//   5) local ref 耗尽语义（null + OutOfMemoryError）
//   6) 深递归护栏（配套缓解验证：SOE 可观测停点，非根因结论）
// native 侧实现由宿主测试注册（tests/core/jni_deep_smoke.cc）。
public class DeepChain {
  // ---- 1) 对象链终止循环（每圈跨 JNI 边界：native 循环 -> guest next -> native ping）----
  private static native long nativeLoop(Object head, int expect);
  public static native long nativePing(long v);

  public static class Node {
    private final int id;
    private final int limit;

    Node(int id, int limit) {
      this.id = id;
      this.limit = limit;
    }

    public Node next() {
      if (id + 1 >= limit) return null;  // 终止条件：正确语义下必须到达
      long tag = DeepChain.nativePing(id);  // 每圈跨 JNI 边界（面包屑同形）
      if (tag != id) return new Node(-1, id + 1);  // 语义偏差哨兵：参数回环错位
      return new Node(id + 1, limit);
    }
  }

  public static long loopProbe(int n) {
    return nativeLoop(new Node(0, n), n);
  }

  // ---- 2) 字符串语义回环 ----
  private static native long nativeStrProbe(int which);

  public static String mkAscii() {
    return "ok";
  }

  public static String mkEmpty() {
    return "";
  }

  public static String mkCn() {
    return "中文串";  // BMP 非 ASCII：MUTF-8 3 字节/字
  }

  public static String mkNul() {
    return new String(new char[] {'a', 0, 'b'});  // 内嵌 NUL：MUTF-8 C0 80
  }

  public static String mkSupp() {
    return new String(new char[] {0xD83D, 0xDE00});  // 增补平面代理对（CESU-8 6 字节）
  }

  public static long strProbe(int which) {
    return nativeStrProbe(which);
  }

  // ---- 3) 接口派发终止循环 ----
  public interface Cursor {
    Cursor next();
  }

  public static class CursorImpl implements Cursor {
    private final int id;
    private final int limit;

    CursorImpl(int id, int limit) {
      this.id = id;
      this.limit = limit;
    }

    public Cursor next() {
      return id + 1 >= limit ? null : new CursorImpl(id + 1, limit);
    }
  }

  private static native long nativeIfaceLoop(Cursor head, int expect);

  public static long ifaceProbe(int n) {
    return nativeIfaceLoop(new CursorImpl(0, n), n);
  }

  // ---- 4/5) local ref churn / 耗尽（见 nativeWrap/nativeExhaust）----
  private static native long nativeWrap(long iters);
  private static native long nativeExhaust();

  public static long wrapProbe(long iters) {
    return nativeWrap(iters);
  }

  public static long exhaustProbe() {
    return nativeExhaust();
  }

  // ---- 6) 深递归护栏（配套缓解验证）----
  private native String nativeStep(long depth, String s);

  public String step(long depth, String s) {
    if (depth <= 0) return s;
    return nativeStep(depth, s);
  }

  public static long guestDeep(long depth) {
    if (depth <= 0) return 0;
    return 1 + guestDeep(depth - 1);
  }

  // ---- 8) 真形态 token 清单遍历（H.d 形态：四步 + 缓存命中快路径）----
  // 模型（真机 oracle）：Java 侧 for-i 遍历 N 条加密 token，每条调 H.d 形态
  // native 解密（GetStringUTFChars->Release->NewStringUTF->CallObjectMethodV
  // intern 四步；重复 token 命中内容缓存快路径；GetMethodID 一次缓存全循环）。
  // 正确语义：循环必然推进、有限圈精确终止（返回 processed == tokens.length）；
  // 返回句柄每笔新 slot（native 侧断言）且为该 token 的 canonical plain。
  private static native String nativeHdDecrypt(String enc);

  public static String hd(String enc) {
    return nativeHdDecrypt(enc);
  }

  public static long tokenWalk(String[] tokens) {
    long processed = 0;
    for (int i = 0; i < tokens.length; i++) {
      String plain = hd(tokens[i]);
      if (plain == null) return -1000L - i;  // 失败哨兵（带停滞位点：非推进实证）
      processed++;
    }
    return processed;
  }

  // 真机分布模型（8454 圈 / 2434 distinct，重尾 ~460x/条）：distinct 池轮转铺开。
  // 池内容 = /tmp/zhihu_oracle golden enc token（真实密文，plain 对照 golden 表）。
  public static String[] makeTokenList(int n) {
    String[] pool = {
      "G6C82C716A6",
      "G6085FB15AB1DAA3DE506",
      "G6586C31FB3",
      "G6490D2",
      "G678CDB1F",
      "G7A88C025BC3FA624E30084",
      "G4482DB0FBE3C992CE107835CF7F7F3DB689AD0089B35B82CF4079144FBFFC6C5",
      "G798FD403BA22EB3CF5079E4FB2C6CCDA798AD91F8B39A62CA6248347FCC1C6C46C91DC1BB339B12CF4",
      "G408DC60EBE3EA82CD61C9F5EFBE1C6C52784D00EF711A82AE91B9E5CDBEBD7D27B85D419BA6AF12AEA0F835BBCEFC2C168CA",
      "G618CD81FFF25B820E809D06BFDE8D3DE6586E113B235EB03F5019E6CF7F6C6C56082D913A535B9",
    };
    String[] out = new String[n];
    for (int i = 0; i < n; i++) out[i] = pool[i % pool.length];
    return out;
  }

  // ---- 9) <clinit> 失败 erroneous 语义（Android：不再重跑）----
  public static class BoomInit {
    static boolean boom() {
      return true;
    }

    static {
      nativeInitTick();
      if (boom()) throw new IllegalStateException("clinit boom");  // javac: 可正常完成
    }

    static void touch() {}
  }

  private static native long nativeInitTick();

  public static long clinitRetryProbe(int triggers) {
    long failures = 0;
    for (int i = 0; i < triggers; i++) {
      try {
        BoomInit.touch();  // active use 触发 <clinit>（只应跑一次）
        return -1;         // 不应到达（<clinit> 必失败）
      } catch (Throwable t) {
        failures++;
      }
    }
    return failures;
  }
}
