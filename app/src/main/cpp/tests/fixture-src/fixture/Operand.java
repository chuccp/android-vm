package fixture;

/** 基类 fixture：实例字段 + 实例方法 + 构造器（供 Calc 继承）。 */
public class Operand {
  public int base;

  public Operand(int b) {
    base = b;
  }

  public int value() {
    return base;
  }
}
