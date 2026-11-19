package javax.xml.datatype;

/**
 * XML Schema 日期时间值类型（最小桩：fastjson/jackson 仅按类型引用；VM 无
 * JAXB 数据绑定面，不做实际时间值语义）。
 */
public abstract class XMLGregorianCalendar implements Cloneable {

    protected XMLGregorianCalendar() {
    }
}
