# JNI 回调

[返回实施手册索引](../art-runtime-v2-implementation.md)

[上一部分](05-jni-core.md) · [下一部分](07-native-libraries.md)

> 本手册为行为规格：实现为全自研 libdexvm.so（不编译 ART），执行语义以
> Android/ART 原生行为为准，third_party/art 源码快照仅作对照。

## 15.16 JNI callback

JNI callback 分两类：

```text
Guest Java -> native method
Guest native/Host -> Guest Java callback
```

### 15.16.1 Java -> native

```text
Guest Java native method
  -> ART native entrypoint
  -> Host Bridge ABI thunk
  -> native function(JNIEnv*, jclass/jobject, args)
  -> native return
  -> Guest JValue/object/exception
```

进入 native function 前：

```text
ART ThreadState = kNative
JNIEnvExt active
JNI local frame active
receiver/class/object args converted to JNI references
wide and floating-point args use target ABI
```

返回后：

```text
read return JValue
native 返回 jobject(local ref) -> JniDecodeReferenceResult 解码为托管对象引用
pop JNI local frame
ART ThreadState restored
pending Guest exception delivered
```

例外：`@FastNative`/`@CriticalNative` 不做 kRunnable→kNative 转换；
`@CriticalNative` 连 JNIEnv/jclass 参数与 local reference frame 都没有。

### 15.16.2 Native -> Java

Native callback 必须使用 Guest `JNIEnv`：

```text
GetEnv(JavaVM, &env, JNI_VERSION_1_6)
  -> JNI_OK: current GuestThread is attached
  -> JNI_EDETACHED: AttachCurrentThread / AttachCurrentThreadAsDaemon
```

Attach 后：

```text
GuestThread peer created
JNIEnvExt created
ThreadRegistered in ART ThreadList
Carrier/GuestTask bound
```

通用 C/C++ 示例：

```cpp
static JavaVM* g_vm = nullptr;
static jobject g_callback = nullptr;
static jmethodID g_on_event = nullptr;

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
    g_vm = vm;
    return JNI_VERSION_1_6;
}

JNIEXPORT void JNICALL
Java_example_NativeBridge_registerCallback(
        JNIEnv* env, jclass, jobject callback) {
    if (g_callback != nullptr) {
        env->DeleteGlobalRef(g_callback);
        g_callback = nullptr;
        g_on_event = nullptr;
    }

    jclass callback_class = env->GetObjectClass(callback);
    g_on_event = env->GetMethodID(callback_class, "onEvent", "(I)V");
    if (g_on_event == nullptr) {
        // GetMethodID 失败时已有 pending NoSuchMethodError。JNI 规范禁止在
        // pending exception 下调用其他 JNI 函数，直接返回，让 pending
        // exception 沿 native 返回路径传播；callback_class 由 local frame 回收。
        return;
    }
    g_callback = env->NewGlobalRef(callback);
    env->DeleteLocalRef(callback_class);
}

void EmitEventFromNativeThread(jint value) {
    JNIEnv* env = nullptr;
    bool attached = false;

    if (g_vm->GetEnv(reinterpret_cast<void**>(&env),
                     JNI_VERSION_1_6) == JNI_EDETACHED) {
        // C++ jni.h 签名为 AttachCurrentThread(JNIEnv**, void*)；
        // C jni.h 才是 (void**, void*)，此处按 C++ 约定传 JNIEnv**。
        if (g_vm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
            return;
        }
        attached = true;
    }

    if (g_callback != nullptr && g_on_event != nullptr) {
        if (env->PushLocalFrame(16) == JNI_OK) {
            env->CallVoidMethod(g_callback, g_on_event, value);
            if (env->ExceptionCheck()) {
                env->ExceptionDescribe();
                env->ExceptionClear();
            }
            env->PopLocalFrame(nullptr);
        }
    }

    if (attached) {
        g_vm->DetachCurrentThread();
    }
}
```

实际实现根据异常策略决定 `ExceptionDescribe/Clear`，或者把 pending Guest
exception 交给 async callback 的 uncaught handler。

长期 callback context 使用锁或 Coroutine Mutex 保护 `class/method/target`
替换，并携带 session generation。回调执行前获取一致的 context snapshot，
避免注册、注销和 callback 并发时出现 stale GlobalRef/jmethodID。

调用步骤：

```text
PushLocalFrame(capacity)
  -> load/cached callback class
  -> GetMethodID or cached jmethodID
  -> convert native event to JNI values
  -> Call<Type>Method / CallStatic<Type>Method
  -> ExceptionCheck
  -> PopLocalFrame
  -> DetachCurrentThread if callback-created the native thread
```

### 15.16.3 Callback class 和 methodID

一次性 callback：

```text
FindClass(callback_descriptor)
  -> GetMethodID/GetStaticMethodID
  -> invoke
```

长期 callback：

```text
NewGlobalRef(callback_class)
GetMethodID
store class/method IDs in native callback context
```

`jmethodID` 在对应 Guest Class 卸载前有效。ClassLoader/Session generation
失效时，native callback context 同时失效。

Native 自己创建的线程中，`FindClass` 使用当前方法声明类的 ClassLoader；
没有 Java frame 时使用 system ClassLoader，再否则 boot ClassLoader。需要
插件 ClassLoader 时，预先缓存 `jclass`，或通过 Guest `ClassLoader.loadClass`
查找。

### 15.16.4 Async callback

```text
Host/Device callback
  -> Bridge generation check
  -> create CallbackEvent(target_thread, target_handler, global refs, args)
  -> Guest callback queue
  -> wake target GuestTask
  -> attach/bind JNIEnv
  -> PushLocalFrame
  -> invoke Guest callback method
  -> deliver exception to Guest Handler/uncaught handler
  -> pop local frame
```

回调目标是 main Looper 时：

```text
Bridge callback
  -> Guest main message queue
  -> wake main GuestTask
  -> dispatch on main thread
```

回调目标是 native-created thread 时：

```text
native callback
  -> GetEnv
  -> AttachCurrentThread if needed
  -> JNI local frame
  -> Guest Java callback
  -> detach on native thread exit
```

### 15.16.5 Callback object reference

```text
callback 期间临时对象
  -> LocalRef

native context 长期保存的 callback object
  -> GlobalRef

可被 GC 回收的 callback target
  -> WeakGlobalRef
  -> 每次 callback 前 NewLocalRef 提升
  -> cleared -> skip callback
```

### 15.16.6 Callback exception

Synchronous callback：

```text
Guest exception
  -> pending on current GuestThread
  -> native code uses ExceptionCheck/ExceptionOccurred
  -> clear or propagate through JNI boundary
```

Asynchronous callback：

```text
Guest exception
  -> Guest Handler/Looper callback
  -> target Thread uncaught exception handler
```

Host Bridge error 先转换为 typed HostResult（定义见
[03-bootstrap-and-class-loading.md](03-bootstrap-and-class-loading.md) 异常投递一节），
再由 Guest framework API 映射为对应 Java exception。Host exception object
不进入 Guest Heap。

