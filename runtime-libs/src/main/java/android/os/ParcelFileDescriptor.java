package android.os;

import java.io.Closeable;
import java.io.File;
import java.io.FileDescriptor;
import java.io.IOException;

/**
 * 文件描述符包（AOSP android.os.ParcelFileDescriptor；§31.7 C 档 lib 骨架——
 * fd 载体；跨进程 fd 归桥接层）。
 */
public class ParcelFileDescriptor implements Parcelable, Closeable {

    private final FileDescriptor mFd;

    public ParcelFileDescriptor(FileDescriptor fd) {
        this.mFd = fd;
    }

    public static ParcelFileDescriptor open(File file, int mode) throws IOException {
        throw new IOException("ParcelFileDescriptor.open: fd 缝后置");
    }

    public static ParcelFileDescriptor dup(FileDescriptor orig) throws IOException {
        throw new IOException("ParcelFileDescriptor.dup: fd 缝后置");
    }

    public static ParcelFileDescriptor adoptFd(int fd) {
        return new ParcelFileDescriptor(new FileDescriptor());
    }

    public static ParcelFileDescriptor fromFd(int fd) throws IOException {
        return new ParcelFileDescriptor(new FileDescriptor());
    }

    public static ParcelFileDescriptor fromSocket(java.net.Socket socket) {
        return new ParcelFileDescriptor(new FileDescriptor());
    }

    public static ParcelFileDescriptor[] createPipe() throws IOException {
        return new ParcelFileDescriptor[] {new ParcelFileDescriptor(new FileDescriptor()),
                                          new ParcelFileDescriptor(new FileDescriptor())};
    }

    public static ParcelFileDescriptor[] createReliablePipe() throws IOException {
        return createPipe();
    }

    public FileDescriptor getFileDescriptor() {
        return this.mFd;
    }

    public int getFd() {
        return -1;
    }

    public long getStatSize() {
        return -1;
    }

    public void close() throws IOException {
    }

    public void detachFd() {
    }

    @Override
    public int describeContents() {
        return 0;
    }

    @Override
    public void writeToParcel(Parcel dest, int flags) {
    }
}
