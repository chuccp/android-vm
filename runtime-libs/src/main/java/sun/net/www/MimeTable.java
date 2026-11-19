package sun.net.www;

// 生成补源（sun-compat）：sun.net.www.MimeTable（URLConnection 表接线）。
public class MimeTable implements java.net.FileNameMap {
    private static final MimeTable INSTANCE = new MimeTable();

    public static java.net.FileNameMap loadTable() {
        return INSTANCE;
    }

    public static MimeTable getDefaultTable() {
        return INSTANCE;
    }

    public java.net.FileNameMap getFileNameMap() {
        return null;
    }

    @Override
    public String getContentTypeFor(String fileName) {
        return null;
    }

}
