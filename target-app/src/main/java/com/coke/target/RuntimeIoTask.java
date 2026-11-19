package com.coke.target;

import android.os.ParcelFileDescriptor;
import android.widget.TextView;
import java.io.FileDescriptor;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;

/**
 * Reader and writer Guest threads backed by a real host pipe. The reader parks
 * in the native scheduler until the write side becomes readable.
 */
public final class RuntimeIoTask implements Runnable {
    private static ParcelFileDescriptor[] pipe;

    private final TextView target;
    private final boolean writer;

    public RuntimeIoTask(TextView target, boolean writer) {
        this.target = target;
        this.writer = writer;
    }

    public static void createPipe() {
        try {
            pipe = ParcelFileDescriptor.createPipe();
        } catch (IOException failure) {
            throw new IllegalStateException(failure);
        }
    }

    @Override
    public void run() {
        try {
            FileDescriptor descriptor = pipe[writer ? 1 : 0].getFileDescriptor();
            if (writer) {
                Thread.sleep(20L);
                FileOutputStream output = new FileOutputStream(descriptor);
                output.write(0x42);
                output.close();
            } else {
                FileInputStream input = new FileInputStream(descriptor);
                int value = input.read();
                input.close();
                target.setText("Guest pipe IO: " + value);
            }
        } catch (InterruptedException interrupted) {
            target.setText("Guest pipe IO interrupted");
            Thread.currentThread().interrupt();
        } catch (IOException failure) {
            target.setText("Guest pipe IO failed");
        }
    }
}
