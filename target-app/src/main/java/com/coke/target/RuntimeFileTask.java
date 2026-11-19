package com.coke.target;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.RandomAccessFile;

/** Exercises the per-session Guest file namespace. */
public final class RuntimeFileTask implements Runnable {
    private final RuntimePanel panel;
    private final File filesDir;

    public RuntimeFileTask(RuntimePanel panel, File filesDir) {
        this.panel = panel;
        this.filesDir = filesDir;
    }

    @Override
    public void run() {
        File file = new File(filesDir, "state/runtime.bin");
        try {
            new File(filesDir, "state").mkdirs();
            FileOutputStream output = new FileOutputStream(file);
            output.write(0x33);
            output.close();

            FileInputStream input = new FileInputStream(file);
            int value = input.read();
            input.close();
            RandomAccessFile random = new RandomAccessFile(file, "rw");
            random.setLength(0L);
            random.writeInt(0x01020304);
            random.seek(0L);
            if (random.readInt() != 0x01020304) {
                throw new IOException("Random access mismatch");
            }
            random.setLength(2L);
            long randomLength = random.length();
            random.getChannel().force(true);
            random.getChannel().close();
            random.close();
            panel.updateFileStatus(
                    "Guest files: " + value + " " +
                            file.getAbsolutePath() + " " + randomLength);
        } catch (IOException failure) {
            panel.updateFileStatus("Guest files failed: " + failure.getMessage());
        }
    }
}
