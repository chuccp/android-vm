package nettest;

import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.net.SocketTimeoutException;
import java.net.UnknownHostException;

/**
 * java.net socket native 面语义用例（Q2 缝：core/builtins.cc POSIX 直通）。
 * 单线程零阻塞口径：预连接（listen backlog 排队）后 accept 立返，数据先写后读，
 * 全链不阻塞。每行输出 "name: OK" 或 "name: FAIL ...";harness 按行判定。
 */
public class NetProbe {

    private static StringBuilder out = new StringBuilder();

    private static void check(String name, boolean ok, String detail) {
        out.append(name).append(": ").append(ok ? "OK" : "FAIL ").append(ok ? "" : detail).append('\n');
    }

    private static byte[] ascii(String s) {
        byte[] b = new byte[s.length()];
        for (int i = 0; i < s.length(); i++) {
            b[i] = (byte) s.charAt(i);
        }
        return b;
    }

    private static String str(byte[] b, int len) {
        char[] c = new char[len];
        for (int i = 0; i < len; i++) {
            c[i] = (char) (b[i] & 0xff);
        }
        return new String(c);
    }

    private static boolean eq(byte[] a, int alen, byte[] b, int blen) {
        if (alen != blen) {
            return false;
        }
        for (int i = 0; i < alen; i++) {
            if (a[i] != b[i]) {
                return false;
            }
        }
        return true;
    }

    private static int readFully(InputStream in, byte[] buf, int off, int len) throws Exception {
        int got = 0;
        while (got < len) {
            int n = in.read(buf, off + got, len - got);
            if (n < 0) {
                break;
            }
            got += n;
        }
        return got;
    }

    public static String run() {
        out = new StringBuilder();
        dnsLocalhost();
        dnsLiteral();
        dnsUnknown();
        echoLoopback();
        socketOptions();
        availableBytes();
        shutdownEof();
        fetchConverge();
        reflectFieldSetDouble();
        return out.toString();
    }

    /** 反射面（设备 libdexjni 链同款查找）：Field.setDouble(Object,double) 按名+签名可寻。 */
    private static void reflectFieldSetDouble() {
        try {
            Class<?>[] ptypes = new Class<?>[] { Object.class, double.class };
            java.lang.reflect.Method m =
                    java.lang.reflect.Field.class.getDeclaredMethod("setDouble", ptypes);
            check("reflect-setdouble", m != null && m.getParameterTypes().length == 2,
                    "m=" + m);
        } catch (Throwable t) {
            check("reflect-setdouble", false, t.toString());
        }
    }

    private static void dnsLocalhost() {
        String step = "getByName";
        try {
            InetAddress a = InetAddress.getByName("localhost");
            step = "getAllByName";
            InetAddress[] all = InetAddress.getAllByName("localhost");
            step = "getHostAddress";
            String ha = a == null ? null : a.getHostAddress();
            step = "length";
            boolean ok = a != null && all != null && all.length >= 1
                    && ha != null && ha.length() > 0;
            check("dns-localhost", ok,
                    "addr=" + (ha == null ? "null" : ha)
                            + " all=" + (all == null ? 0 : all.length));
        } catch (Throwable t) {
            check("dns-localhost", false, "step=" + step + " " + t.toString());
        }
    }

    private static void dnsLiteral() {
        try {
            InetAddress v4 = InetAddress.getByName("127.0.0.1");
            InetAddress v6 = InetAddress.getByName("::1");
            InetAddress ext = InetAddress.getByName("8.8.8.8");
            boolean ok = v4 != null && v4.isLoopbackAddress() && v6 != null
                    && ext != null && "8.8.8.8".equals(ext.getHostAddress());
            check("dns-literal", ok,
                    "v4=" + (v4 == null ? "null" : v4.getHostAddress())
                            + " v6=" + (v6 == null ? "null" : v6.getHostAddress())
                            + " ext=" + (ext == null ? "null" : ext.getHostAddress()));
        } catch (Throwable t) {
            check("dns-literal", false, t.toString());
        }
    }

    private static void dnsUnknown() {
        try {
            InetAddress.getByName("no-such-host.invalid");
            check("dns-unknown", false, "no UnknownHostException");
        } catch (UnknownHostException uhe) {
            check("dns-unknown", true, "");
        } catch (Throwable t) {
            check("dns-unknown", false, t.toString());
        }
    }

    /** 回环 echo：预连接 + accept + 双向写读（全链零阻塞）。 */
    private static void echoLoopback() {
        ServerSocket server = null;
        Socket client = null;
        Socket peer = null;
        try {
            InetAddress loop = InetAddress.getByName("127.0.0.1");
            server = new ServerSocket(0, 1, loop);
            int port = server.getLocalPort();
            client = new Socket();
            client.connect(new InetSocketAddress(loop, port), 3000);
            peer = server.accept();
            byte[] msg = ascii("ping-123");
            OutputStream cOut = client.getOutputStream();
            InputStream cIn = client.getInputStream();
            OutputStream pOut = peer.getOutputStream();
            InputStream pIn = peer.getInputStream();
            cOut.write(msg);
            byte[] up = new byte[msg.length];
            int un = readFully(pIn, up, 0, up.length);
            pOut.write(up, 0, un);
            byte[] back = new byte[msg.length];
            int bn = readFully(cIn, back, 0, back.length);
            boolean peerOk = peer.getInetAddress() != null
                    && peer.getInetAddress().isLoopbackAddress()
                    && peer.getPort() == client.getLocalPort()
                    && client.getLocalPort() > 0;
            check("echo-loopback", eq(msg, msg.length, back, bn) && un == msg.length && peerOk,
                    "up=" + str(up, un) + " back=" + str(back, bn)
                            + " peer=" + peer.getInetAddress() + ":" + peer.getPort()
                            + " local=" + client.getLocalPort());
        } catch (Throwable t) {
            check("echo-loopback", false, t.toString());
        } finally {
            closeQuietly(client);
            closeQuietly(peer);
            closeQuietly(server);
        }
    }

    private static void socketOptions() {
        Socket s = null;
        try {
            s = new Socket();
            s.setTcpNoDelay(true);
            s.setSoTimeout(1234);
            s.setReuseAddress(true);
            s.setKeepAlive(true);
            boolean ok = s.getTcpNoDelay() && s.getSoTimeout() == 1234
                    && s.getReuseAddress() && s.getKeepAlive();
            check("socket-options", ok,
                    "nodelay=" + s.getTcpNoDelay() + " timeout=" + s.getSoTimeout()
                            + " reuse=" + s.getReuseAddress() + " keep=" + s.getKeepAlive());
        } catch (Throwable t) {
            check("socket-options", false, t.toString());
        } finally {
            closeQuietly(s);
        }
    }

    /** available 面：peer 先写，client available() 应见在途字节（回环投递限时重试）。 */
    private static void availableBytes() {
        ServerSocket server = null;
        Socket client = null;
        Socket peer = null;
        try {
            InetAddress loop = InetAddress.getByName("127.0.0.1");
            server = new ServerSocket(0, 1, loop);
            client = new Socket();
            client.connect(new InetSocketAddress(loop, server.getLocalPort()), 3000);
            peer = server.accept();
            peer.getOutputStream().write(ascii("abcd"));
            int avail = 0;
            for (int i = 0; i < 200; i++) {
                avail = client.getInputStream().available();
                if (avail >= 4) {
                    break;
                }
                try {
                    Thread.sleep(1);
                } catch (Throwable t) {
                    break;
                }
            }
            check("available-bytes", avail >= 4, "avail=" + avail);
        } catch (Throwable t) {
            check("available-bytes", false, t.toString());
        } finally {
            closeQuietly(client);
            closeQuietly(peer);
            closeQuietly(server);
        }
    }

    /** shutdownOutput：peer 半关后 client 读完在途数据落 EOF。 */
    private static void shutdownEof() {
        ServerSocket server = null;
        Socket client = null;
        Socket peer = null;
        try {
            InetAddress loop = InetAddress.getByName("127.0.0.1");
            server = new ServerSocket(0, 1, loop);
            client = new Socket();
            client.connect(new InetSocketAddress(loop, server.getLocalPort()), 3000);
            peer = server.accept();
            peer.getOutputStream().write(0x2a);
            peer.shutdownOutput();
            InputStream in = client.getInputStream();
            int b = in.read();
            int eof = in.read();
            check("shutdown-eof", b == 0x2a && eof == -1, "b=" + b + " eof=" + eof);
        } catch (Throwable t) {
            check("shutdown-eof", false, t.toString());
        } finally {
            closeQuietly(client);
            closeQuietly(peer);
            closeQuietly(server);
        }
    }

    /**
     * fetch 收敛语义：先探失败端口（ConnectException = fetch 失败轮），再走
     * HTTP/1.0 形态 mock server（本进程预连接回环）成功（fetch 成功轮）——
     * 有限重试内收敛即 OK（zonfig 配置 fetch 的最小收敛形态）。
     */
    private static void fetchConverge() {
        String body = null;
        String fail1 = "";
        try {
            // 失败轮：占位端口关闭后 connect = ECONNREFUSED
            ServerSocket probe = new ServerSocket(0, 1, InetAddress.getByName("127.0.0.1"));
            int deadPort = probe.getLocalPort();
            probe.close();
            try {
                Socket dead = new Socket();
                dead.connect(new InetSocketAddress(
                        InetAddress.getByName("127.0.0.1"), deadPort), 500);
                dead.close();
                fail1 = "unexpected connect success";
            } catch (Throwable expected) {
                fail1 = "ok:" + expected.getClass().getName();
            }

            // 成功轮：HTTP/1.0 GET /config → 200 + body
            ServerSocket server = null;
            Socket client = null;
            Socket peer = null;
            try {
                InetAddress loop = InetAddress.getByName("127.0.0.1");
                server = new ServerSocket(0, 1, loop);
                client = new Socket();
                client.connect(new InetSocketAddress(loop, server.getLocalPort()), 3000);
                peer = server.accept();
                byte[] req = ascii("GET /config HTTP/1.0\r\nHost: localhost\r\n\r\n");
                client.getOutputStream().write(req);
                byte[] reqGot = new byte[req.length];
                int rn = readFully(peer.getInputStream(), reqGot, 0, reqGot.length);
                byte[] resp = ascii("HTTP/1.0 200 OK\r\nContent-Length: 2\r\n\r\nok");
                peer.getOutputStream().write(resp);
                byte[] respGot = new byte[resp.length];
                int sn = readFully(client.getInputStream(), respGot, 0, respGot.length);
                body = str(respGot, sn);
                check("fetch-converge",
                        fail1.startsWith("ok:") && rn == req.length && sn == resp.length
                                && body.startsWith("HTTP/1.0 200") && body.endsWith("ok"),
                        "fail1=" + fail1 + " req=" + rn + " resp=" + body);
            } finally {
                closeQuietly(client);
                closeQuietly(peer);
                closeQuietly(server);
            }
        } catch (Throwable t) {
            check("fetch-converge", false, "fail1=" + fail1 + " " + t.toString());
        }
    }

    private static void closeQuietly(Socket s) {
        if (s != null) {
            try {
                s.close();
            } catch (Throwable t) {
                // ignore
            }
        }
    }

    private static void closeQuietly(ServerSocket s) {
        if (s != null) {
            try {
                s.close();
            } catch (Throwable t) {
                // ignore
            }
        }
    }
}
