package com.dexvm.host;

import android.util.Log;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.ConnectException;
import java.net.InetSocketAddress;
import java.net.SocketException;
import java.net.SocketTimeoutException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.security.PublicKey;
import java.security.cert.Certificate;
import java.security.cert.X509Certificate;
import java.util.Collection;
import java.util.List;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.ThreadFactory;
import java.util.concurrent.atomic.AtomicLong;

import javax.net.ssl.SSLException;
import javax.net.ssl.SSLParameters;
import javax.net.ssl.SSLSession;
import javax.net.ssl.SSLSocket;
import javax.net.ssl.SSLSocketFactory;

/**
 * guest TLS -> 宿主真 TLS 栈 桥接线程（与 core/tls_bridge.cc / NativeBridge 配对）。
 *
 * <p>guest 的 {@code SSLSocketFactory}/SSLSocket 薄壳把 open/write/read/close 编成
 * 请求，经 {@code nativeTlsTake()} 阻塞取到本线程；本线程用宿主
 * {@link SSLSocketFactory#getDefault()} 建立真实 TLS 连接（宿主 DNS/SNI/证书链/
 * 主机名校验，不跳过校验），把结果经 {@code nativeTlsReply()} 投回 guest。
 *
 * <p>线程模型：单一分发线程 + 有界语义的 IO 线程池（每连接同一时刻至多一个
 * read/write，CLOSE 从分发线程直接关宿主 socket 以打断阻塞 read）。
 *
 * <p>线格式（小端，见 core/tls_bridge.h）：
 * <pre>
 * op = kind:u8 | id:i64 | handle:i64 | port:i32 | timeout:i32 |
 *      maxBytes:i32 | hostLen:i32 | dataLen:i32 | host[hostLen] | data[dataLen]
 * OPEN 成功回复 data = 文本元数据（key=value 行：protocol/cipher/subject/
 * issuer/san=type|value/spki=alg|hex/addr=ip）。
 * </pre>
 */
public final class TlsBridge {
    private static final String TAG = "DexVmTls";

    // 与 core/tls_bridge.h 对齐
    private static final int OP_OPEN = 1;
    private static final int OP_WRITE = 2;
    private static final int OP_READ = 3;
    private static final int OP_CLOSE = 4;

    private static final int STATUS_OK = 0;
    private static final int STATUS_EOF = 1;
    private static final int STATUS_IO_ERROR = 2;
    private static final int STATUS_TIMEOUT = 3;
    private static final int STATUS_SSL_ERROR = 4;
    private static final int STATUS_CONNECT_ERROR = 5;
    private static final int STATUS_CLOSED = 6;

    private static final int MAX_REPLY_BYTES = 64 * 1024;

    private static final Object START_LOCK = new Object();
    private static Thread bridgeThread;

    private static final AtomicLong NEXT_HANDLE = new AtomicLong();
    private static final ConcurrentHashMap<Long, Conn> CONNS = new ConcurrentHashMap<>();

    private static final ExecutorService IO_POOL =
            Executors.newCachedThreadPool(new ThreadFactory() {
                private final AtomicLong seq = new AtomicLong();

                @Override
                public Thread newThread(Runnable r) {
                    Thread t = new Thread(r, "dexvm-tls-io-" + seq.incrementAndGet());
                    t.setDaemon(true);
                    return t;
                }
            });

    private TlsBridge() {
    }

    /** 幂等启动（宿主进程生命周期常驻；Activity onCreate 调用）。 */
    public static void start() {
        synchronized (START_LOCK) {
            if (bridgeThread != null && bridgeThread.isAlive()) return;
            bridgeThread = new Thread(TlsBridge::bridgeLoop, "dexvm-tls-bridge");
            bridgeThread.setDaemon(true);
            bridgeThread.start();
            Log.i(TAG, "bridge thread started");
        }
    }

    private static void bridgeLoop() {
        for (;;) {
            byte[] op;
            try {
                op = NativeBridge.nativeTlsTake();
            } catch (Throwable t) {
                Log.e(TAG, "nativeTlsTake failed", t);
                break;
            }
            if (op == null) {
                Log.i(TAG, "bridge thread exiting (native shutdown)");
                break;
            }
            try {
                dispatch(op);
            } catch (Throwable t) {
                Log.e(TAG, "dispatch failed", t);
            }
        }
        for (Conn conn : CONNS.values()) conn.closeQuietly();
        CONNS.clear();
    }

    private static void dispatch(byte[] op) {
        if (op == null || op.length < 1 + 8 + 8 + 4 + 4 + 4 + 4 + 4) {
            Log.w(TAG, "malformed op (" + (op == null ? -1 : op.length) + " bytes)");
            return;
        }
        ByteBuffer buf = ByteBuffer.wrap(op).order(ByteOrder.LITTLE_ENDIAN);
        int kind = buf.get() & 0xFF;
        long id = buf.getLong();
        long handle = buf.getLong();
        int port = buf.getInt();
        int timeoutMs = buf.getInt();
        int maxBytes = buf.getInt();
        int hostLen = buf.getInt();
        int dataLen = buf.getInt();
        if (hostLen < 0 || dataLen < 0 || hostLen > buf.remaining()
                || dataLen > buf.remaining() - hostLen) {
            reply(id, STATUS_IO_ERROR, 0, "malformed TLS op lengths");
            return;
        }
        byte[] hostBytes = new byte[hostLen];
        buf.get(hostBytes);
        byte[] data = new byte[dataLen];
        buf.get(data);
        String host = new String(hostBytes, StandardCharsets.UTF_8);

        switch (kind) {
            case OP_OPEN: {
                final String target = host;
                final int targetPort = port;
                final int connectTimeout = timeoutMs;
                IO_POOL.execute(() -> doOpen(id, target, targetPort, connectTimeout));
                break;
            }
            case OP_WRITE: {
                Conn conn = CONNS.get(handle);
                if (conn == null) {
                    Log.w(TAG, "write handle=" + handle + " closed (no conn)");
                    reply(id, STATUS_CLOSED, handle, "TLS handle is closed");
                    break;
                }
                IO_POOL.execute(() -> {
                    try {
                        conn.write(data);
                        Log.i(TAG, "write ok handle=" + handle + " bytes=" + data.length
                                + " [" + preview(data) + "]");
                        reply(id, STATUS_OK, 0, (byte[]) null);
                    } catch (Throwable t) {
                        Log.w(TAG, "write failed handle=" + handle + " " + describeError(t));
                        reply(id, statusFor(t), 0, describeError(t));
                    }
                });
                break;
            }
            case OP_READ: {
                Conn conn = CONNS.get(handle);
                if (conn == null) {
                    Log.w(TAG, "read handle=" + handle + " closed (no conn)");
                    reply(id, STATUS_CLOSED, handle, "TLS handle is closed");
                    break;
                }
                final int readTimeout = timeoutMs;
                final int want = maxBytes <= 0 ? 8192 : Math.min(maxBytes, MAX_REPLY_BYTES);
                IO_POOL.execute(() -> {
                    try {
                        byte[] part = conn.read(want, readTimeout);
                        if (part == null) {
                            Log.i(TAG, "read handle=" + handle + " EOF");
                            reply(id, STATUS_EOF, 0, (byte[]) null);
                        } else {
                            Log.i(TAG, "read ok handle=" + handle + " bytes=" + part.length
                                    + " [" + preview(part) + "]");
                            reply(id, STATUS_OK, 0, part);
                        }
                    } catch (Throwable t) {
                        Log.w(TAG, "read failed handle=" + handle + " " + describeError(t));
                        reply(id, statusFor(t), 0, describeError(t));
                    }
                });
                break;
            }
            case OP_CLOSE: {
                Conn conn = CONNS.remove(handle);
                Log.i(TAG, "close handle=" + handle + (conn == null ? " (absent)" : ""));
                if (conn != null) conn.closeQuietly();
                reply(id, STATUS_OK, handle, (byte[]) null);
                break;
            }
            default:
                reply(id, STATUS_IO_ERROR, 0, "unknown TLS op kind " + kind);
                break;
        }
    }

    private static void doOpen(long id, String host, int port, int connectTimeoutMs) {
        SSLSocket socket = null;
        try {
            SSLSocketFactory factory = (SSLSocketFactory) SSLSocketFactory.getDefault();
            socket = (SSLSocket) factory.createSocket();
            // 宿主栈做真证书链 + HTTPS 主机名校验（与平台 HTTPS 客户端同口径）
            SSLParameters parameters = socket.getSSLParameters();
            parameters.setEndpointIdentificationAlgorithm("HTTPS");
            socket.setSSLParameters(parameters);
            int timeout = connectTimeoutMs > 0 ? connectTimeoutMs : 15000;
            socket.connect(new InetSocketAddress(host, port), timeout);
            socket.setSoTimeout(0);
            socket.startHandshake();
            long handle = NEXT_HANDLE.incrementAndGet();
            Conn conn = new Conn(handle, socket);
            CONNS.put(handle, conn);
            reply(id, STATUS_OK, handle, describeSession(conn));
            Log.i(TAG, "open ok " + host + ":" + port + " handle=" + handle
                    + " protocol=" + conn.session.getProtocol());
        } catch (Throwable t) {
            closeQuietly(socket);
            Log.w(TAG, "open failed " + host + ":" + port + " " + describeError(t));
            reply(id, statusFor(t), 0, describeError(t));
        }
    }

    /** 宿主握手结果 -> guest 会话/证书身份投影（行编码）。 */
    private static String describeSession(Conn conn) {
        StringBuilder out = new StringBuilder();
        SSLSession session = conn.session;
        appendLine(out, "protocol", session.getProtocol());
        appendLine(out, "cipher", session.getCipherSuite());
        try {
            if (conn.socket.getInetAddress() != null) {
                appendLine(out, "addr", conn.socket.getInetAddress().getHostAddress());
            }
        } catch (Throwable ignored) {
        }
        try {
            Certificate[] chain = session.getPeerCertificates();
            if (chain != null && chain.length > 0 && chain[0] instanceof X509Certificate) {
                X509Certificate leaf = (X509Certificate) chain[0];
                try {
                    appendLine(out, "subject", leaf.getSubjectX500Principal().getName());
                } catch (Throwable ignored) {
                }
                try {
                    appendLine(out, "issuer", leaf.getIssuerX500Principal().getName());
                } catch (Throwable ignored) {
                }
                try {
                    Collection<List<?>> sans = leaf.getSubjectAlternativeNames();
                    if (sans != null) {
                        for (List<?> entry : sans) {
                            if (entry == null || entry.size() < 2) continue;
                            Object type = entry.get(0);
                            Object value = entry.get(1);
                            if (type instanceof Integer && value instanceof String) {
                                appendLine(out, "san",
                                        ((Integer) type).intValue() + "|" + value);
                            }
                        }
                    }
                } catch (Throwable ignored) {
                }
                try {
                    PublicKey key = leaf.getPublicKey();
                    if (key != null && key.getEncoded() != null) {
                        appendLine(out, "spki",
                                key.getAlgorithm() + "|" + hex(key.getEncoded()));
                    }
                } catch (Throwable ignored) {
                }
            }
        } catch (Throwable ignored) {
        }
        return out.toString();
    }

    private static void appendLine(StringBuilder out, String key, String value) {
        if (value == null) return;
        // 行协议：值内不得含换行（DN/SAN 不会出现；防御性替换）。
        String clean = value.replace('\n', ' ').replace('\r', ' ');
        out.append(key).append('=').append(clean).append('\n');
    }

    private static String hex(byte[] bytes) {
        char[] digits = "0123456789abcdef".toCharArray();
        char[] out = new char[bytes.length * 2];
        for (int i = 0; i < bytes.length; i++) {
            int v = bytes[i] & 0xFF;
            out[i * 2] = digits[v >>> 4];
            out[i * 2 + 1] = digits[v & 0xF];
        }
        return new String(out);
    }

    private static int statusFor(Throwable t) {
        Throwable root = t;
        int depth = 0;
        while (root.getCause() != null && root.getCause() != root && depth++ < 4) {
            root = root.getCause();
        }
        if (t instanceof SocketTimeoutException || root instanceof SocketTimeoutException) {
            return STATUS_TIMEOUT;
        }
        if (t instanceof SSLException || root instanceof SSLException) {
            return STATUS_SSL_ERROR;
        }
        if (t instanceof ConnectException || root instanceof ConnectException) {
            return STATUS_CONNECT_ERROR;
        }
        if (t instanceof SocketException || root instanceof SocketException) {
            String message = String.valueOf(t.getMessage());
            if (message.toLowerCase().contains("closed")) return STATUS_CLOSED;
            return STATUS_IO_ERROR;
        }
        return STATUS_IO_ERROR;
    }

    private static String describeError(Throwable t) {
        String message = t.getClass().getName();
        String detail = t.getMessage();
        if (detail != null) message += ": " + detail;
        if (message.length() > 4000) message = message.substring(0, 4000);
        return message;
    }

    /** 明文协议预览（诊断面）：up to 160 bytes, non-printables as '.'。 */
    private static String preview(byte[] data) {
        if (data == null) return "";
        // TEMP DIAG: 160 -> 1024（udid_guest 响应体完整归因；事后恢复 160）。
        int n = Math.min(data.length, 1024);
        StringBuilder sb = new StringBuilder(n);
        for (int i = 0; i < n; i++) {
            int c = data[i] & 0xFF;
            if (c == '\r') {
                sb.append("\\r");
            } else if (c == '\n') {
                sb.append("\\n");
            } else if (c >= 0x20 && c < 0x7F) {
                sb.append((char) c);
            } else {
                sb.append('.');
            }
        }
        return sb.toString();
    }

    private static void reply(long id, int status, long handle, String text) {
        reply(id, status, handle, text == null ? null : text.getBytes(StandardCharsets.UTF_8));
    }

    private static void reply(long id, int status, long handle, byte[] data) {
        try {
            NativeBridge.nativeTlsReply(id, status, handle, data);
        } catch (Throwable t) {
            Log.e(TAG, "nativeTlsReply failed id=" + id, t);
        }
    }

    private static void closeQuietly(SSLSocket socket) {
        if (socket == null) return;
        try {
            socket.close();
        } catch (Throwable ignored) {
        }
    }

    private static final class Conn {
        final long handle;
        final SSLSocket socket;
        final InputStream input;
        final OutputStream output;
        final SSLSession session;

        Conn(long handle, SSLSocket socket) throws IOException {
            this.handle = handle;
            this.socket = socket;
            this.input = socket.getInputStream();
            this.output = socket.getOutputStream();
            this.session = socket.getSession();
        }

        void write(byte[] data) throws IOException {
            output.write(data);
            output.flush();
        }

        /** null = EOF。 */
        byte[] read(int maxBytes, int soTimeoutMs) throws IOException {
            socket.setSoTimeout(soTimeoutMs <= 0 ? 0 : soTimeoutMs);
            int size = Math.max(1, Math.min(maxBytes, MAX_REPLY_BYTES));
            byte[] buffer = new byte[size];
            int n = input.read(buffer);
            if (n < 0) return null;
            if (n == 0) return new byte[0];
            byte[] part = new byte[n];
            System.arraycopy(buffer, 0, part, 0, n);
            return part;
        }

        void closeQuietly() {
            try {
                socket.close();
            } catch (Throwable ignored) {
            }
        }
    }
}
