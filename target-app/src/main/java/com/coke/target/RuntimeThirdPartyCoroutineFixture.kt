package com.coke.target

import com.fasterxml.jackson.databind.ObjectMapper
import com.fasterxml.jackson.core.util.DefaultPrettyPrinter
import com.fasterxml.jackson.core.util.Separators
import com.alibaba.fastjson.JSON
import com.alibaba.fastjson.JSONPath
import com.alibaba.fastjson2.JSON as JSON2
import com.alibaba.fastjson2.JSONPath as JSONPath2
import com.google.gson.Gson
import io.netty.bootstrap.Bootstrap
import io.netty.bootstrap.ServerBootstrap
import io.netty.buffer.Unpooled
import io.netty.buffer.ByteBuf
import io.netty.channel.ChannelHandlerContext
import io.netty.channel.ChannelInboundHandlerAdapter
import io.netty.channel.ChannelInitializer
import io.netty.channel.DefaultEventLoopGroup
import io.netty.channel.SimpleChannelInboundHandler
import io.netty.channel.embedded.EmbeddedChannel
import io.netty.channel.nio.NioEventLoopGroup
import io.netty.channel.socket.SocketChannel
import io.netty.channel.socket.nio.NioServerSocketChannel
import io.netty.channel.socket.nio.NioSocketChannel
import io.netty.handler.codec.ByteToMessageDecoder
import io.netty.handler.codec.MessageToByteEncoder
import io.netty.util.concurrent.DefaultPromise
import io.netty.util.concurrent.ImmediateEventExecutor
import io.netty.util.internal.logging.InternalLoggerFactory
import io.netty.util.internal.logging.JdkLoggerFactory
import io.reactivex.rxjava3.core.Completable
import io.reactivex.rxjava3.core.Flowable
import io.reactivex.rxjava3.core.Maybe
import io.reactivex.rxjava3.core.Observable
import io.reactivex.rxjava3.core.Single
import io.reactivex.rxjava3.schedulers.Schedulers
import io.reactivex.rxjava3.subjects.PublishSubject
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.async
import kotlinx.coroutines.cancelAndJoin
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.filter
import kotlinx.coroutines.flow.flow
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.reduce
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.supervisorScope
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeoutOrNull
import okio.Buffer
import org.apache.commons.lang3.ArrayUtils
import java.util.concurrent.Callable
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicInteger
import java.util.concurrent.atomic.AtomicReference
import java.net.InetSocketAddress

/**
 * Third-party JVM libraries and Kotlin coroutines executed from target APK DEX.
 *
 * The methods deliberately return stable sentinels so the instrumentation test
 * can distinguish a successful library result from a partially compatible path.
 */
class RuntimeThirdPartyCoroutineFixture {
    fun structuredConcurrencyFlow(): Int = runBlocking {
        val left = async {
            delay(30)
            20
        }
        val right = async(Dispatchers.Default) {
            delay(10)
            21
        }
        val completed = CompletableDeferred<Int>()
        val producer = launch {
            delay(5)
            completed.complete(42)
        }
        producer.join()
        val total = left.await() + right.await() + completed.await()
        check(total == 83)
        check(coroutineContext.isActive)
        301
    }

    fun channelFlow(): Int = runBlocking {
        val channel = Channel<Int>(capacity = 1)
        val consumer = launch {
            var total = 0
            for (value in channel) {
                total += value
            }
            check(total == 123)
        }
        channel.send(42)
        channel.send(81)
        channel.close()
        consumer.join()
        302
    }

    fun flowPipelineFlow(): Int = runBlocking {
        val emitted = mutableListOf<Int>()
        val reduced = flow {
            for (value in 1..8) {
                delay(1)
                emit(value)
            }
        }
            .map { value -> value * 3 }
            .filter { value -> value % 2 == 0 }
            .reduce { left, right -> left + right }
        emitted += reduced
        check(reduced == 60)
        coroutineScope {
            launch {
                delay(100)
                check(false)
            }.cancelAndJoin()
        }
        303
    }

    fun timeoutAndCancellationFlow(): Int = runBlocking {
        val timedOut = withTimeoutOrNull(20) {
            delay(250)
            1
        }
        check(timedOut == null)
        coroutineScope {
            launch {
                delay(100)
                check(false)
            }.cancelAndJoin()
        }
        304
    }

    fun supervisorIsolationFlow(): Int = runBlocking {
        supervisorScope {
            val failed = async<Int> {
                delay(5)
                throw IllegalStateException("expected")
            }
            val healthy = async {
                delay(15)
                7
            }
            check(runCatching { failed.await() }.isFailure)
            check(healthy.await() == 7)
        }
        305
    }

    fun thirdPartyLibrariesFlow(): Int {
        val mapper = ObjectMapper()
        val parsed = mapper.readTree(
            """{"displayName":"vm","count":3,"ignored":true}""",
        )
        val jacksonJson = parsed.toString()
        if (parsed.path("displayName").asText() != "vm") return 411
        if (parsed.path("count").asInt() != 3) return 412
        if (!jacksonJson.contains("\"count\":3")) return 413

        val gson = Gson()
        val decoded = gson.fromJson(
            jacksonJson,
            Map::class.java,
        )
        if (decoded["displayName"] != "vm") return 414

        if (Buffer()
                .writeUtf8("vm-coroutines")
                .sha256()
                .hex() !=
            "138057622124c210770d0936e8f6bd1bc3e161b29b995bd3a8fcb161a4530685"
        ) {
            return 416
        }
        if (ArrayUtils.indexOf(intArrayOf(4, 8, 15, 16), 15) != 2) {
            return 418
        }
        if (!ArrayUtils.isNotEmpty(intArrayOf(1))) return 419
        return 401
    }

    fun jacksonInitializationFlow(): Int {
        val spacing = Separators.Spacing.BOTH
        if (spacing.spacesBefore() != " ") return -2
        if (spacing.spacesAfter() != " ") return -3
        if (spacing.apply(':') != " : ") return -4

        val separators = Separators()
        val rootSeparator = separators.rootSeparator
        if (rootSeparator != " ") return -51
        if (separators.objectFieldValueSpacing != spacing) return -6

        DefaultPrettyPrinter(separators)
        return 402
    }

    fun fastJsonFlow(): Int {
        val parsed = JSON.parseObject(
            """{"displayName":"vm","count":3}""",
        )
        if (parsed.getString("displayName") != "vm") return 431
        if (parsed.getIntValue("count") != 3) return 432

        val encoded = JSON.toJSONString(parsed)
        if (!encoded.contains("\"displayName\":\"vm\"")) return 433

        val bean = JSON.parseObject(
            encoded,
            RuntimeFastJsonValue::class.java,
        )
        if (bean.displayName != "vm") return 434
        if (bean.count != 3) return 435
        val converted = parsed.toJavaObject(
            RuntimeFastJsonValue::class.java,
        )
        if (converted.displayName != "vm") return 436
        if (converted.count != 3) return 437
        if (JSONPath.eval(parsed, "$.displayName") != "vm") {
            return 438
        }
        return 403
    }

    fun fastJson2Flow(): Int {
        val parsed = JSON2.parseObject(
            """{"displayName":"vm","count":3}""",
        )
        if (parsed.getString("displayName") != "vm") return 441
        if (parsed.getIntValue("count") != 3) return 442

        val encoded = JSON2.toJSONString(parsed)
        if (!encoded.contains("\"displayName\":\"vm\"")) return 443

        val bean = JSON2.parseObject(
            encoded,
            RuntimeFastJsonValue::class.java,
        )
        if (bean.displayName != "vm") return 444
        if (bean.count != 3) return 445
        if (JSONPath2.eval(parsed, "$.count") != 3) return 446
        return 404
    }

    fun nettyByteBufFlow(): Int {
        configureNettyLogging()
        val buffer = Unpooled.buffer()
        try {
            buffer.writeInt(0x01020304)
            buffer.writeByte(0x6e)
            buffer.writeShort(0x7474)
            buffer.writeLong(0x1020304050607080L)
            if (buffer.readableBytes() != 15) return 451
            buffer.markReaderIndex()
            if (buffer.readInt() != 0x01020304) return 452
            if (buffer.readUnsignedByte().toInt() != 0x6e) return 453
            if (buffer.readUnsignedShort().toInt() != 0x7474) return 457
            if (buffer.readLong() != 0x1020304050607080L) return 458
            buffer.resetReaderIndex()
            val copy = buffer.copy()
            try {
                if (!copy.isReadable || copy.readUnsignedByte().toInt() != 1) {
                    return 454
                }
                if (copy.getUnsignedByte(
                        copy.writerIndex() - 1,
                    ).toInt() == 0
                ) {
                    return 455
                }
            } finally {
                copy.release()
            }
            if (!buffer.release()) return 456
            return 405
        } finally {
            if (buffer.refCnt() != 0) buffer.release()
        }
    }

    fun nettyPromiseFlow(): Int {
        configureNettyLogging()
        val promise = DefaultPromise<Int>(ImmediateEventExecutor.INSTANCE)
        val listenerRan = AtomicBoolean()
        promise.addListener { future ->
            if (future.isSuccess) listenerRan.set(true)
        }
        promise.setSuccess(42)
        if (!promise.await(1, TimeUnit.SECONDS)) return 462
        if (!promise.isSuccess || promise.getNow() != 42) return 463
        if (!listenerRan.get()) return 464
        return 406
    }

    fun nettyEmbeddedChannelFlow(): Int {
        configureNettyLogging()
        val channel = EmbeddedChannel(
            RuntimeIntToByteEncoder(),
            RuntimeByteToIntDecoder(),
        )
        try {
            if (!channel.isActive) return 471
            if (!channel.writeOutbound(42)) return 472
            val framed = channel.readOutbound<ByteBuf>()
                ?: return 473
            if (framed.readableBytes() != 4) return 474
            if (!channel.writeInbound(framed)) return 475
            val decoded = channel.readInbound<Int>()
            if (decoded != 42) return 476
            return 407
        } finally {
            channel.finishAndReleaseAll()
        }
    }

    fun nettyEventLoopFlow(): Int {
        configureNettyLogging()
        val callerThread = Thread.currentThread().name
        val group = DefaultEventLoopGroup(1)
        try {
            val eventLoop = group.next()
            val threadName = AtomicReference<String>()
            val promise = eventLoop.newPromise<Int>()
            eventLoop.execute {
                threadName.set(Thread.currentThread().name)
                promise.setSuccess(42)
            }
            if (!promise.await(2, TimeUnit.SECONDS)) return 481
            if (!promise.isSuccess || promise.getNow() != 42) return 482
            if (threadName.get() == callerThread) return 483
            return 408
        } finally {
            group.shutdownGracefully().syncUninterruptibly()
        }
    }

    fun nettyNioEventLoopFlow(): Int {
        configureNettyLogging()
        val callerThread = Thread.currentThread().name
        val group = NioEventLoopGroup(1)
        try {
            val eventLoop = group.next()
            val threadName = AtomicReference<String>()
            val promise = eventLoop.newPromise<Int>()
            eventLoop.execute {
                threadName.set(Thread.currentThread().name)
                promise.setSuccess(84)
            }
            if (!promise.await(3, TimeUnit.SECONDS)) return 491
            if (!promise.isSuccess || promise.getNow() != 84) return 492
            if (threadName.get() == callerThread) return 493
            val submittedFuture = eventLoop.submit(
                Callable { 20 + 22 },
            )
            var remainingChecks = 100
            while (!submittedFuture.isDone && remainingChecks-- > 0) {
                Thread.sleep(10)
            }
            if (!submittedFuture.isDone) return 495
            val submitted =
                submittedFuture.get(0, TimeUnit.MILLISECONDS)
            if (submitted != 42) return 494
            return 409
        } finally {
            group.shutdownGracefully(0, 0, TimeUnit.MILLISECONDS)
        }
    }

    fun nettyNioTcpFlow(): Int = runBlocking {
        configureNettyLogging()
        val serverGroup = NioEventLoopGroup(1)
        val clientGroup = NioEventLoopGroup(1)
        val received = CompletableDeferred<Int>()
        try {
            val serverBootstrap = ServerBootstrap()
                .group(serverGroup)
                .channel(NioServerSocketChannel::class.java)
                .childHandler(
                    object : ChannelInitializer<SocketChannel>() {
                        override fun initChannel(ch: SocketChannel) {
                            ch.pipeline().addLast(
                                RuntimeByteToIntDecoder(),
                                RuntimeNioInboundHandler(received),
                            )
                        }
                    },
                )
            val bindFuture = serverBootstrap.bind(0)
            var bindChecks = 500
            while (!bindFuture.isDone && bindChecks-- > 0) {
                delay(10)
            }
            if (!bindFuture.isDone) return@runBlocking 502
            val listener = bindFuture.sync().channel()
            val port = (listener.localAddress() as InetSocketAddress).port

            val clientBootstrap = Bootstrap()
                .group(clientGroup)
                .channel(NioSocketChannel::class.java)
                .handler(
                    object : ChannelInitializer<SocketChannel>() {
                        override fun initChannel(ch: SocketChannel) {
                            ch.pipeline().addLast(
                                RuntimeIntToByteEncoder(),
                            )
                        }
                    },
                )
            val connectFuture =
                clientBootstrap.connect("127.0.0.1", port)
            var connectChecks = 500
            while (!connectFuture.isDone && connectChecks-- > 0) {
                delay(10)
            }
            if (!connectFuture.isDone) return@runBlocking 503
            val client = connectFuture.sync().channel()
            val writeFuture = client.writeAndFlush(42)
            var writeChecks = 500
            while (!writeFuture.isDone && writeChecks-- > 0) {
                delay(10)
            }
            if (!writeFuture.isDone) return@runBlocking 504
            writeFuture.sync()
            val value = withTimeoutOrNull(5_000) {
                received.await()
            }
            if (value != 42) return@runBlocking 501
            client.close().syncUninterruptibly()
            listener.close().syncUninterruptibly()
            return@runBlocking 410
        } finally {
            serverGroup.shutdownGracefully(0, 0, TimeUnit.MILLISECONDS)
            clientGroup.shutdownGracefully(0, 0, TimeUnit.MILLISECONDS)
        }
    }

    fun rxJavaObservableFlow(): Int {
        val mapped = Observable.just(1, 2, 3, 4)
            .map { value -> value * 3 }
            .filter { value -> value % 2 == 0 }
            .toList()
            .blockingGet()
        val zipped = Observable.zip(
            Observable.just(10, 20),
            Observable.just(1, 2, 3),
        ) { left, right -> left + right }
            .toList()
            .blockingGet()
        val flattened = Observable.just(1, 2, 3)
            .flatMap { value -> Observable.just(value, value * 10) }
            .toList()
            .blockingGet()
        val single = Single.just(20).map { value -> value + 22 }.blockingGet()
        val maybe = Maybe.just(7).map { value -> value * 6 }.blockingGet()
        val completions = AtomicInteger()
        Completable.fromAction { completions.incrementAndGet() }
            .blockingAwait()

        val checks =
            (if (mapped.size == 2 &&
                mapped[0] == 6 &&
                mapped[1] == 12
            ) 1 else 0) or
                (if (zipped.size == 2 &&
                    zipped[0] == 11 &&
                    zipped[1] == 22
                ) 2 else 0) or
                (if (flattened.size == 6 &&
                    flattened[0] == 1 &&
                    flattened[1] == 10 &&
                    flattened[2] == 2 &&
                    flattened[3] == 20 &&
                    flattened[4] == 3 &&
                    flattened[5] == 30
                ) 4 else 0) or
                (if (single == 42) 8 else 0) or
                (if (maybe == 42) 16 else 0) or
                (if (completions.get() == 1) 32 else 0)
        return if (checks == 63) 411 else 1_000 + checks
    }

    fun rxJavaSchedulersAndBackpressureFlow(): Int {
        val callerThread = Thread.currentThread().name
        val observedThread = AtomicReference<String>()
        val total = AtomicInteger()
        Flowable.range(1, 100)
            .map { value -> value * 2 }
            .observeOn(Schedulers.computation())
            .doOnNext { value ->
                total.addAndGet(value)
                observedThread.set(Thread.currentThread().name)
            }
            .blockingSubscribe()

        val ioThread = AtomicReference<String>()
        val ioValue = Single.fromCallable {
            ioThread.set(Thread.currentThread().name)
            42
        }
            .subscribeOn(Schedulers.io())
            .blockingGet()
        val ticks = Observable.interval(
            1,
            TimeUnit.MILLISECONDS,
            Schedulers.computation(),
        )
            .take(3)
            .map { value -> value + 1 }
            .toList()
            .blockingGet()

        val checks =
            (if (total.get() == 10_100) 1 else 0) or
                (if (observedThread.get() != null &&
                    observedThread.get() != callerThread
                ) 2 else 0) or
                (if (ioValue == 42) 4 else 0) or
                (if (ioThread.get() != null &&
                    ioThread.get() != callerThread
                ) 8 else 0) or
                (if (ticks.size == 3 &&
                    ticks[0] == 1L &&
                    ticks[1] == 2L &&
                    ticks[2] == 3L
                ) 16 else 0)
        return if (checks == 31) 412 else 512
    }

    fun rxJavaErrorAndCancellationFlow(): Int {
        val errorCount = AtomicInteger()
        val recovered = Observable.just(0)
            .concatWith(Observable.error<Int>(
                IllegalStateException("expected-rxjava"),
            ))
            .doOnError { errorCount.incrementAndGet() }
            .onErrorReturnItem(42)
            .toList()
            .blockingGet()

        val disposed = AtomicBoolean()
        val emptyDisposable = Observable.never<Int>()
            .doOnDispose { disposed.set(true) }
            .subscribe()
        emptyDisposable.dispose()

        val subject = PublishSubject.create<Int>()
        val received = AtomicInteger()
        val subjectDisposable = subject.subscribe { value ->
            received.set(value)
        }
        subject.onNext(5)
        subjectDisposable.dispose()
        subject.onNext(7)

        val checks =
            (if (recovered.size == 2 &&
                recovered[0] == 0 &&
                recovered[1] == 42
            ) 1 else 0) or
                (if (errorCount.get() == 1) 2 else 0) or
                (if (disposed.get()) 4 else 0) or
                (if (received.get() == 5) 8 else 0)
        return if (checks == 15) 413 else 513
    }

    private fun configureNettyLogging() {
        InternalLoggerFactory.setDefaultFactory(JdkLoggerFactory.INSTANCE)
    }
}

private class RuntimeIntToByteEncoder : MessageToByteEncoder<Int>() {
    override fun encode(
        ctx: ChannelHandlerContext,
        msg: Int,
        out: ByteBuf,
    ) {
        out.writeInt(msg)
    }
}

private class RuntimeByteToIntDecoder : ByteToMessageDecoder() {
    override fun decode(
        ctx: ChannelHandlerContext,
        input: ByteBuf,
        out: MutableList<Any>,
    ) {
        if (input.readableBytes() < 4) return
        out.add(input.readInt())
    }
}

private class RuntimeNioInboundHandler(
    private val received: CompletableDeferred<Int>,
) : ChannelInboundHandlerAdapter() {
    override fun channelRead(
        ctx: ChannelHandlerContext,
        msg: Any,
    ) {
        if (msg is Int) received.complete(msg)
    }
}
