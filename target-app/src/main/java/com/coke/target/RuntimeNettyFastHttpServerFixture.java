package com.coke.target;

import io.netty.bootstrap.ServerBootstrap;
import io.netty.buffer.ByteBuf;
import io.netty.buffer.Unpooled;
import io.netty.channel.Channel;
import io.netty.channel.ChannelHandlerContext;
import io.netty.channel.ChannelInboundHandlerAdapter;
import io.netty.channel.ChannelInitializer;
import io.netty.channel.EventLoopGroup;
import io.netty.channel.nio.NioEventLoopGroup;
import io.netty.channel.socket.SocketChannel;
import io.netty.channel.socket.nio.NioServerSocketChannel;
import io.netty.util.CharsetUtil;
import io.netty.util.ReferenceCountUtil;
import io.netty.util.internal.logging.InternalLoggerFactory;
import io.netty.util.internal.logging.JdkLoggerFactory;

import java.util.concurrent.TimeUnit;

public final class RuntimeNettyFastHttpServerFixture {
    private static final int DEFAULT_PORT = 18080;
    private static final int DEFAULT_SECONDS = 600;
    private static final String BODY =
            "Hello from Android VM via Netty HTTP\n";
    private static final String RESPONSE =
            "HTTP/1.1 200 OK\r\n"
                    + "Content-Type: text/plain; charset=UTF-8\r\n"
                    + "Content-Length: "
                    + BODY.length()
                    + "\r\n\r\n"
                    + BODY;

    private RuntimeNettyFastHttpServerFixture() {
    }

    public static int fastHttpServerFlow() {
        return fastHttpServerFlow(
                intEnvironment("DEXVM_HTTP_PORT", DEFAULT_PORT),
                intEnvironment("DEXVM_HTTP_SECONDS", DEFAULT_SECONDS));
    }

    public static int fastHttpServerFlow(int port, int seconds) {
        System.setProperty("io.netty.noKeySetOptimization", "true");
        InternalLoggerFactory.setDefaultFactory(JdkLoggerFactory.INSTANCE);
        EventLoopGroup group = new NioEventLoopGroup(1);
        Channel listener = null;
        try {
            listener = new ServerBootstrap()
                    .group(group)
                    .channel(NioServerSocketChannel.class)
                    .childHandler(new ChannelInitializer<SocketChannel>() {
                        @Override
                        protected void initChannel(SocketChannel channel) {
                            channel.pipeline().addLast(
                                    new ChannelInboundHandlerAdapter() {
                                        @Override
                                        public void channelRead(
                                                ChannelHandlerContext context,
                                                Object message) {
                                            if (!(message
                                                    instanceof ByteBuf)) {
                                                ReferenceCountUtil.release(
                                                        message);
                                                return;
                                            }
                                            ByteBuf request = (ByteBuf) message;
                                            try {
                                                context.writeAndFlush(
                                                        Unpooled.copiedBuffer(
                                                                RESPONSE,
                                                                CharsetUtil
                                                                        .UTF_8));
                                            } finally {
                                                request.release();
                                            }
                                        }

                                        @Override
                                        public void exceptionCaught(
                                                ChannelHandlerContext context,
                                                Throwable cause) {
                                            context.close();
                                        }
                                    });
                        }

                        @Override
                        public boolean isSharable() {
                            return true;
                        }
                    })
                    .bind("127.0.0.1", port)
                    .sync()
                    .channel();

            final Channel closingListener = listener;
            closingListener.eventLoop().schedule(
                    () -> closingListener.close(),
                    Math.max(1, seconds),
                    TimeUnit.SECONDS);
            closingListener.closeFuture().sync();
            return 622;
        } catch (Throwable failure) {
            return 623;
        } finally {
            if (listener != null) {
                listener.close().syncUninterruptibly();
            }
            group.shutdownGracefully(
                    0,
                    0,
                    TimeUnit.MILLISECONDS);
        }
    }

    private static int intEnvironment(
            String name,
            int fallback) {
        String value = System.getenv(name);
        if (value == null || value.isEmpty()) {
            return fallback;
        }
        try {
            return Integer.parseInt(value);
        } catch (NumberFormatException ignored) {
            return fallback;
        }
    }
}
