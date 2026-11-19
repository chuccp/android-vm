package android.util;

/**
 * XML 属性集接口（AOSP android.util.AttributeSet；§31.7 C 档 lib 接口面）。
 * 实现面 = 资源解析缝（XmlBlock$Parser 内建 / 测试桩）。
 */
public interface AttributeSet {

    int getAttributeCount();

    String getAttributeName(int index);

    String getAttributeValue(int index);

    String getAttributeValue(String namespace, String name);

    String getPositionDescription();

    int getAttributeNameResource(int index);

    int getAttributeListValue(String namespace, String attribute, String[] options,
                              int defaultValue);

    boolean getAttributeBooleanValue(String namespace, String attribute, boolean defaultValue);

    int getAttributeResourceValue(String namespace, String attribute, int defaultValue);

    int getAttributeIntValue(String namespace, String attribute, int defaultValue);

    int getAttributeUnsignedIntValue(String namespace, String attribute, int defaultValue);

    float getAttributeFloatValue(String namespace, String attribute, float defaultValue);

    int getAttributeListValue(int index, String[] options, int defaultValue);

    boolean getAttributeBooleanValue(int index, boolean defaultValue);

    int getAttributeResourceValue(int index, int defaultValue);

    int getAttributeIntValue(int index, int defaultValue);

    int getAttributeUnsignedIntValue(int index, int defaultValue);

    float getAttributeFloatValue(int index, float defaultValue);

    String getClassAttribute();

    int getStyleAttribute();
}
