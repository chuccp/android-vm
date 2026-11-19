package com.coke.target;

import io.netty.bootstrap.Bootstrap;
import io.netty.bootstrap.ServerBootstrap;
import io.netty.buffer.ByteBuf;
import io.netty.channel.Channel;
import io.netty.channel.ChannelHandlerContext;
import io.netty.channel.ChannelInboundHandlerAdapter;
import io.netty.channel.ChannelInitializer;
import io.netty.channel.EventLoopGroup;
import io.netty.channel.nio.NioEventLoopGroup;
import io.netty.channel.socket.SocketChannel;
import io.netty.channel.socket.nio.NioServerSocketChannel;
import io.netty.channel.socket.nio.NioSocketChannel;
import io.netty.handler.codec.ByteToMessageDecoder;
import io.netty.handler.codec.MessageToByteEncoder;
import io.netty.util.ReferenceCountUtil;
import io.netty.util.internal.logging.InternalLoggerFactory;
import io.netty.util.internal.logging.JdkLoggerFactory;

import java.net.InetSocketAddress;
import java.util.List;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;

public final class RuntimeNettyServerFixture {
    private RuntimeNettyServerFixture() {
    }

    public static int serverFlow() {
        System.setProperty("io.netty.noKeySetOptimization", "true");
        InternalLoggerFactory.setDefaultFactory(JdkLoggerFactory.INSTANCE);
        EventLoopGroup bossGroup = new NioEventLoopGroup(1);
        EventLoopGroup serverGroup = new NioEventLoopGroup(1);
        EventLoopGroup clientGroup = new NioEventLoopGroup(1);
        Channel listener = null;
        Channel client = null;
        AtomicInteger serverValue = new AtomicInteger(-1);
        AtomicInteger clientValue = new AtomicInteger(-1);
        AtomicBoolean pipelineFailure = new AtomicBoolean();
        try {
            listener = new ServerBootstrap()
                    .group(bossGroup, serverGroup)
                    .channel(NioServerSocketChannel.class)
                    .childHandler(new ChannelInitializer<SocketChannel>() {
                        @Override
                        protected void initChannel(SocketChannel channel) {
                            channel.pipeline().addLast(
                                    new IntDecoder(),
                                    new IntEncoder(),
                                    new ChannelInboundHandlerAdapter() {
                                        @Override
                                        public void channelRead(
                                                ChannelHandlerContext context,
                                                Object message) {
                                            try {
                                                int value = (Integer) message;
                                                serverValue.set(value);
                                                context.writeAndFlush(
                                                        Integer.valueOf(value + 1));
                                            } finally {
                                                ReferenceCountUtil.release(message);
                                            }
                                        }

                                        @Override
                                        public void exceptionCaught(
                                                ChannelHandlerContext context,
                                                Throwable cause) {
                                            pipelineFailure.set(true);
                                            context.close();
                                        }
                                    });
                        }
                    })
                    .bind(0)
                    .sync()
                    .channel();

            int port =
                    ((InetSocketAddress) listener.localAddress()).getPort();
            client = new Bootstrap()
                    .group(clientGroup)
                    .channel(NioSocketChannel.class)
                    .handler(new ChannelInitializer<SocketChannel>() {
                        @Override
                        protected void initChannel(SocketChannel channel) {
                            channel.pipeline().addLast(
                                    new IntEncoder(),
                                    new IntDecoder(),
                                    new ChannelInboundHandlerAdapter() {
                                        @Override
                                        public void channelRead(
                                                ChannelHandlerContext context,
                                                Object message) {
                                            try {
                                                clientValue.set(
                                                        (Integer) message);
                                            } finally {
                                                ReferenceCountUtil.release(
                                                        message);
                                            }
                                        }

                                        @Override
                                        public void exceptionCaught(
                                                ChannelHandlerContext context,
                                                Throwable cause) {
                                            pipelineFailure.set(true);
                                            context.close();
                                        }
                                    });
                        }
                    })
                    .connect("127.0.0.1", port)
                    .sync()
                    .channel();

            client.writeAndFlush(Integer.valueOf(41)).sync();
            for (int attempt = 0;
                    attempt < 500 && clientValue.get() < 0;
                    attempt++) {
                Thread.sleep(10);
            }

            if (pipelineFailure.get()) {
                return 604;
            }
            if (serverValue.get() != 41) {
                return 602;
            }
            if (clientValue.get() != 42) {
                return 603;
            }
            return 610;
        } catch (Throwable failure) {
            return 601;
        } finally {
            if (client != null) {
                client.close().syncUninterruptibly();
            }
            if (listener != null) {
                listener.close().syncUninterruptibly();
            }
            bossGroup.shutdownGracefully(
                    0,
                    0,
                    TimeUnit.MILLISECONDS);
            serverGroup.shutdownGracefully(
                    0,
                    0,
                    TimeUnit.MILLISECONDS);
            clientGroup.shutdownGracefully(
                    0,
                    0,
                    TimeUnit.MILLISECONDS);
        }
    }

    private static final class IntEncoder extends MessageToByteEncoder<Integer> {
        @Override
        protected void encode(
                ChannelHandlerContext context,
                Integer value,
                ByteBuf output) {
            output.writeInt(value);
        }
    }

    private static final class IntDecoder extends ByteToMessageDecoder {
        @Override
        protected void decode(
                ChannelHandlerContext context,
                ByteBuf input,
                List<Object> output) {
            if (input.readableBytes() < 4) {
                return;
            }
            output.add(Integer.valueOf(input.readInt()));
        }
    }
}
