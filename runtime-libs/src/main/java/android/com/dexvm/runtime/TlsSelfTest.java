package com.dexvm.runtime;

import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.security.KeyStore;
import java.security.cert.Certificate;
import java.security.cert.X509Certificate;
import java.util.Collection;
import java.util.List;

import javax.net.ssl.SSLContext;
import javax.net.ssl.SSLSession;
import javax.net.ssl.SSLSocket;
import javax.net.ssl.SSLSocketFactory;
import javax.net.ssl.TrustManager;
import javax.net.ssl.TrustManagerFactory;

/**
 * 设备 TLS 桥接自检（guest 侧）：与宿主 TlsBridge.java / core/tls_bridge.cc 走
 * 完整通路（OkHttp 同款 SSLContext + TrustManagerFactory + createSocket(wrap)）。
 *
 * 由 NativeBridge.nativeRunTlsSelfTest 引导 runtime.dex 后反射调用；返回
 * "OK ..."/"FAIL ..." 诊断串（logcat tag DexVmTlsSelfTest）。
 */
public final class TlsSelfTest {
    private TlsSelfTest() {
    }

    public static String run(String host, int port) {
        Socket raw = null;
        SSLSocket socket = null;
        StringBuilder out = new StringBuilder();
        try {
            TrustManagerFactory tmf =
                    TrustManagerFactory.getInstance(TrustManagerFactory.getDefaultAlgorithm());
            tmf.init((KeyStore) null);
            TrustManager[] managers = tmf.getTrustManagers();
            out.append("tm=").append(managers == null ? 0 : managers.length);

            SSLContext context = SSLContext.getInstance("TLS");
            context.init(null, managers, null);
            SSLSocketFactory factory = context.getSocketFactory();
            out.append(" factory=").append(factory.getClass().getName());

            // 同时验证 getDefault() 面（不建连）
            if (SSLSocketFactory.getDefault() == null) {
                return "FAIL SSLSocketFactory.getDefault() == null";
            }

            raw = new Socket();
            raw.connect(new InetSocketAddress(host, port), 15000);
            socket = (SSLSocket) factory.createSocket(raw, host, port, true);
            socket.setSoTimeout(15000);
            socket.startHandshake();

            SSLSession session = socket.getSession();
            out.append(" proto=").append(session.getProtocol());
            out.append(" cipher=").append(session.getCipherSuite());
            Certificate[] chain = session.getPeerCertificates();
            out.append(" chain=").append(chain == null ? 0 : chain.length);
            if (chain != null && chain.length > 0 && chain[0] instanceof X509Certificate) {
                Collection<List<?>> sans =
                        ((X509Certificate) chain[0]).getSubjectAlternativeNames();
                out.append(" sans=").append(sans == null ? 0 : sans.size());
            }

            String request = "GET / HTTP/1.1\r\nHost: " + host
                    + "\r\nConnection: close\r\nUser-Agent: dexvm-tls-selftest\r\n\r\n";
            OutputStream output = socket.getOutputStream();
            output.write(request.getBytes("UTF-8"));
            output.flush();

            InputStream input = socket.getInputStream();
            byte[] buffer = new byte[1024];
            int n = input.read(buffer);
            if (n <= 0) {
                return "FAIL no response (" + out + ")";
            }
            String head = new String(buffer, 0, n);
            int lineEnd = head.indexOf('\n');
            String status = lineEnd > 0 ? head.substring(0, lineEnd).trim() : head.trim();
            out.append(" status=").append(status);
            return (status.startsWith("HTTP/") ? "OK " : "FAIL ") + out;
        } catch (Throwable t) {
            return "FAIL " + t + " (" + out + ")";
        } finally {
            if (socket != null) {
                try {
                    socket.close();
                } catch (Throwable ignored) {
                }
            }
            if (raw != null) {
                try {
                    raw.close();
                } catch (Throwable ignored) {
                }
            }
        }
    }
}
