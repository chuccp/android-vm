package com.coke.target;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.ServerSocket;
import java.net.Socket;

/**
 * Exercises non-blocking Guest socket connect/accept/read/write through the
 * native cooperative scheduler.
 */
public final class RuntimeSocketTask implements Runnable {
    private static volatile int port;

    private final RuntimePanel target;
    private final boolean server;

    public RuntimeSocketTask(RuntimePanel target, boolean server) {
        this.target = target;
        this.server = server;
    }

    @Override
    public void run() {
        try {
            if (server) {
                runServer();
            } else {
                runClient();
            }
        } catch (IOException failure) {
            target.updateSocketStatus("Guest socket failed: " + failure.getMessage());
        }
    }

    private void runServer() throws IOException {
        ServerSocket listener = new ServerSocket(0);
        port = listener.getLocalPort();
        Socket accepted = listener.accept();
        InputStream input = accepted.getInputStream();
        int value = input.read();
        target.updateSocketStatus("Guest socket: " + value);
        input.close();
        accepted.close();
        listener.close();
    }

    private void runClient() throws IOException {
        while (port == 0) {
            Thread.yield();
        }
        Socket client = new Socket("127.0.0.1", port);
        OutputStream output = client.getOutputStream();
        output.write(0x5A);
        output.close();
        client.close();
    }
}
