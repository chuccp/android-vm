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
import io.netty.handler.codec.http.DefaultFullHttpResponse;
import io.netty.handler.codec.http.FullHttpResponse;
import io.netty.handler.codec.http.HttpHeaderNames;
import io.netty.handler.codec.http.HttpRequest;
import io.netty.handler.codec.http.HttpResponseStatus;
import io.netty.handler.codec.http.HttpServerCodec;
import io.netty.handler.codec.http.HttpVersion;
import io.netty.util.CharsetUtil;
import io.netty.util.ReferenceCountUtil;
import io.netty.util.internal.logging.InternalLoggerFactory;
import io.netty.util.internal.logging.JdkLoggerFactory;

import java.util.concurrent.TimeUnit;

public final class RuntimeNettyHttpServerFixture {
    private static final int DEFAULT_PORT = 18080;
    private static final int DEFAULT_SECONDS = 600;

    private RuntimeNettyHttpServerFixture() {
    }

    public static int httpServerFlow() {
        System.setProperty("io.netty.noKeySetOptimization", "true");
        InternalLoggerFactory.setDefaultFactory(JdkLoggerFactory.INSTANCE);
        final int port = intEnvironment("DEXVM_HTTP_PORT", DEFAULT_PORT);
        final int seconds =
                intEnvironment("DEXVM_HTTP_SECONDS", DEFAULT_SECONDS);
        EventLoopGroup bossGroup = new NioEventLoopGroup(1);
        EventLoopGroup workerGroup = new NioEventLoopGroup(1);
        Channel listener = null;
        try {
            listener = new ServerBootstrap()
                    .group(bossGroup, workerGroup)
                    .channel(NioServerSocketChannel.class)
                    .childHandler(new ChannelInitializer<SocketChannel>() {
                        @Override
                        protected void initChannel(SocketChannel channel) {
                            channel.pipeline().addLast(
                                    new HttpServerCodec(),
                                    new ChannelInboundHandlerAdapter() {
                                        @Override
                                        public void channelRead(
                                                ChannelHandlerContext context,
                                                Object message) {
                                            try {
                                                if (!(message
                                                        instanceof HttpRequest)) {
                                                    return;
                                                }
                                                String body =
                                                        "Hello from Android VM "
                                                                + "on Windows "
                                                                + "via Netty "
                                                                + "HTTP\n";
                                                ByteBuf content =
                                                        Unpooled.copiedBuffer(
                                                                body,
                                                                CharsetUtil
                                                                        .UTF_8);
                                                FullHttpResponse response =
                                                        new DefaultFullHttpResponse(
                                                                HttpVersion
                                                                        .HTTP_1_1,
                                                                HttpResponseStatus
                                                                        .OK,
                                                                content);
                                                response.headers().set(
                                                        HttpHeaderNames
                                                                .CONTENT_TYPE,
                                                        "text/plain; "
                                                                + "charset=UTF-8");
                                                response.headers().setInt(
                                                        HttpHeaderNames
                                                                .CONTENT_LENGTH,
                                                        content.readableBytes());
                                                context.writeAndFlush(response);
                                            } finally {
                                                ReferenceCountUtil.release(
                                                        message);
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
            return 620;
        } catch (Throwable failure) {
            return 621;
        } finally {
            if (listener != null) {
                listener.close().syncUninterruptibly();
            }
            bossGroup.shutdownGracefully(
                    0,
                    0,
                    TimeUnit.MILLISECONDS);
            workerGroup.shutdownGracefully(
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
