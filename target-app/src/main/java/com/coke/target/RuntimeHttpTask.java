package com.coke.target;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.ServerSocket;
import java.net.Socket;

/** Exercises a local HTTP server and a plain Socket HTTP client. */
public final class RuntimeHttpTask implements Runnable {
    private static final String BODY = "pong-from-guest";
    private static volatile int port;

    private final RuntimePanel panel;
    private final boolean server;

    public RuntimeHttpTask(RuntimePanel panel, boolean server) {
        this.panel = panel;
        this.server = server;
    }

    @Override
    public void run() {
        try {
            if (server) runServer(); else runClient();
        } catch (Throwable failure) {
            panel.updateHttpStatus("Guest HTTP failed: " + failure.getMessage());
        }
    }

    private void runServer() throws IOException {
        ServerSocket listener = new ServerSocket(0);
        port = listener.getLocalPort();
        Socket accepted = listener.accept();
        InputStream input = accepted.getInputStream();
        readHeaders(input);
        String response = "HTTP/1.1 200 OK\r\n"
                + "Content-Length: " + BODY.length() + "\r\n"
                + "Connection: close\r\n\r\n" + BODY;
        OutputStream output = accepted.getOutputStream();
        for (int i = 0; i < response.length(); i++) output.write(response.charAt(i));
        output.close();
        input.close();
        accepted.close();
        listener.close();
    }

    private void readHeaders(InputStream input) throws IOException {
        int matched = 0;
        while (matched < 4) {
            int value = input.read();
            if (value < 0) throw new IOException("HTTP client closed before headers");
            matched = (matched == 0 || matched == 2) && value == '\r'
                    ? matched + 1
                    : (matched == 1 || matched == 3) && value == '\n'
                    ? matched + 1 : value == '\r' ? 1 : 0;
        }
    }

    private void runClient() throws IOException {
        while (port == 0) Thread.yield();
        Socket client = new Socket("127.0.0.1", port);
        OutputStream output = client.getOutputStream();
        String request = "GET /ping HTTP/1.1\r\nHost: 127.0.0.1\r\n"
                + "Connection: close\r\n\r\n";
        for (int i = 0; i < request.length(); i++) output.write(request.charAt(i));
        InputStream input = client.getInputStream();
        StringBuilder response = new StringBuilder();
        int value;
        while ((value = input.read()) >= 0) response.append((char) value);
        String text = response.toString();
        panel.updateHttpStatus(
                text.length() > 0
                        ? "Guest HTTP: 200 " + BODY
                        : "Guest HTTP: invalid response");
        input.close();
        output.close();
        client.close();
    }
}
