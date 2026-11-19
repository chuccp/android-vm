package fixture;

/**
 * 主 fixture 类：静态/实例字段与方法、构造器、字符串常量（含内嵌 NUL）、
 * try/catch、switch、数组、接口实现、继承。
 * 静态常量字段用于 EncodedArray(static_values) 解码测试。
 */
public class Calc extends Operand implements Shape {
  // ---- 静态常量（static_values EncodedArray）----
  public static final boolean Z_TRUE = true;
  public static final boolean Z_FALSE = false;
  public static final byte B = (byte) -5;
  public static final short S = (short) -300;
  public static final char C = 'A';
  public static final int I = 123456;
  public static final int ZERO = 0;
  public static final long L = 0x123456789ABCDEFL;
  public static final float F = 1.5f;
  public static final double D = -2.25;
  public static final String STR = "hi";
  public static final String NULSTR = "a" + (char) 0 + "b";
  public static final Class<?> CLS = Calc.class;
  public static final Object NUL = null;
  public static final int AFTER = 7;

  // ---- 实例字段 ----
  public int width;
  public int height;

  // ---- 构造器 ----
  public Calc() {
    this(1, 1);
  }

  public Calc(int w, int h) {
    super(w + h);
    width = w;
    height = h;
  }

  // ---- 静态方法 ----
  public static int add(int a, int b) {
    return a + b;
  }

  /** proto 参数列表：(I Ljava/lang/String; D Lfixture/Calc;)J */
  public static long mix(int a, String s, double d, Calc c) {
    return a + s.length() + (long) d + c.width;
  }

  /** try/catch：tries_size > 0 */
  public static int safeDiv(int a, int b) {
    try {
      return a / b;
    } catch (ArithmeticException e) {
      return -1;
    }
  }

  /** packed-switch */
  public static int pick(int x) {
    switch (x) {
      case 1:
        return 10;
      case 2:
        return 20;
      case 3:
        return 30;
      default:
        return -1;
    }
  }

  /** fill-array-data payload */
  public static int[] table() {
    return new int[] {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
  }

  /** 字符串常量 + StringBuilder 拼接 */
  public static String greet(String name) {
    return "hello, " + name;
  }

  // ---- 实例方法 / 接口实现 ----
  @Override
  public int area() {
    return width * height;
  }

  public int mul(int k) {
    return area() * k;
  }
}
