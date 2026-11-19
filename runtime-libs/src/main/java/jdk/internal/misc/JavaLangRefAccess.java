package jdk.internal.misc;

public interface JavaLangRefAccess {
    boolean waitForReferenceProcessing() throws InterruptedException;
    void runFinalization();
}
