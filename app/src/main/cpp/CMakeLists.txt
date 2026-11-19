# libdexvm.so —— 全自研 DEX VM（无 ART/AOSP 编译依赖）
# 目录即模块（docs/art-runtime-v2/01-foundation.md §3）
cmake_minimum_required(VERSION 3.22.1)
project(dexvm CXX ASM)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-Wall -Wextra -Wno-unused-parameter -fno-rtti -fno-exceptions
                    -fvisibility=hidden)

# ART 对齐：设备侧运行时是优化构建（官方 ART 解释器/运行时以 -O2 编译）。
# Android Gradle 的 Debug 变体默认不给 -O 标志（clang 默认 -O0），switch 解释器
# 的启动链会被放大数倍；Debug 也强制 -O2（保留 -g 便于栈归因）。
add_compile_options($<$<CONFIG:Debug>:-O2>)

# Android 构建带完整 session IO（apk/jar zip 解析，session/class_loader.cc）
add_compile_definitions(DEXVM_SESSION_IO)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

add_library(dexvm_dex STATIC
  dex/dex_file.cc
  dex/dex_verify.cc
  dex/leb128.cc
  dex/mutf8.cc
  dex/opcodes.cc
)

add_library(dexvm_memory STATIC
  memory/heap.cc
  memory/handle_table.cc
)

add_library(dexvm_coroutine STATIC
  coroutine/scheduler.cc
  coroutine/guest_task.cc
  coroutine/fiber_switch.S
)

add_library(dexvm_runtime STATIC
  core/object_model.cc
  core/class_linker.cc
  core/interpreter.cc
  core/exception.cc
  core/builtins.cc
  core/builtins_string.cc
  core/builtins_system.cc
  core/builtins_thread.cc
  core/builtins_unsafe.cc
  core/builtins_reflect.cc
  core/builtins_io.cc
  core/builtins_ref.cc
  core/builtins_exceptions.cc
  core/builtins_classloader.cc
  core/tls_bridge.cc
  core/sensor_bridge.cc
  core/framework_sensor.cc
  core/notification_bridge.cc
  core/framework_notification.cc
  core/framework_builtins.cc
  core/framework_digest.cc
  core/framework_collections.cc
  core/framework_font.cc
  core/framework_fragment.cc
  core/framework_inflate.cc
  core/framework_media.cc
  core/framework_net.cc
  core/framework_os.cc
  core/framework_app.cc
  core/framework_view.cc
  core/intern_table.cc
  core/gc.cc
  core/native_hooks.cc
)

add_library(dexvm_jni STATIC
  jni/handle_table.cc
  jni/jni_env.cc
  jni/native_manager.cc
  jni/native_shield.S
  jni/reflection.cc
)

add_library(dexvm_session STATIC
  session/session.cc
  session/class_loader.cc
)

add_library(dexvm_binder STATIC
  binder/binder_domain.cc
)

add_library(dexvm_framework STATIC
  framework/framework.cc
  framework/lifecycle_driver.cc
  framework/render.cc
)

add_library(dexvm_bridge STATIC
  bridge/bridge.cc
  bridge/ui_bridge_protocol.cc
)

add_library(dexvm SHARED
  jni/runtime_contract.cpp
  bridge/ui_bridge_jni.cc
)

target_link_libraries(dexvm
  PRIVATE
    dexvm_runtime
    dexvm_jni
    dexvm_session
    dexvm_framework
    dexvm_bridge
    dexvm_binder
    dexvm_coroutine
    dexvm_memory
    dexvm_dex
    z
    log
)

# 对 App 只导出 JNI contract symbol（JNIEXPORT 已带 default visibility）
target_link_options(dexvm PRIVATE -Wl,--exclude-libs,ALL)
