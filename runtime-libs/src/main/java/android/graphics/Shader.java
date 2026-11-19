package android.graphics;

/**
 * 着色器基类（AOSP android.graphics.Shader；§31.7 C 档 lib 骨架）。
 * 仅 Paint.setShader/getShader 引用面载体；渐变族语义后置。
 */
public class Shader {
    public Shader() {
    }

    public boolean getLocalMatrix(Matrix localM) {
        return false;
    }

    public void setLocalMatrix(Matrix localM) {
    }
}
