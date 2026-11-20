plugins {
    id("com.android.application")
}

android {
    namespace = "com.dexvm.host"
    compileSdk = 35

    defaultConfig {
        applicationId = "com.dexvm.host"
        minSdk = 31
        targetSdk = 35
        versionCode = 1
        versionName = "0.1"

        ndk {
            abiFilters += listOf("arm64-v8a", "x86_64")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    // D0.5 起接入 native 构建；CMakeLists 就位后自动启用
    if (file("src/main/cpp/CMakeLists.txt").exists()) {
        externalNativeBuild {
            cmake {
                path = file("src/main/cpp/CMakeLists.txt")
                version = "3.22.1"
            }
        }
    }

    sourceSets {
        getByName("main") {
            // 设备考 guest APK：target-app 构建产出落生成 assets（不入 git；
            // MainActivity copyAsset("target-app-debug.apk") 消费）
            assets.srcDir("${layout.buildDirectory.get().asFile}/generated/targetAssets")
            // boot classpath（runtime.dex）：runtime-libs dex 任务产出自动落位
            //（禁止手抄 app/src/main/assets/runtime.dex——陈旧副本事故根因）
            assets.srcDir("${layout.buildDirectory.get().asFile}/generated/runtimeAssets")
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
        }
    }
}

dependencies {
    // 宿主物化面：guest androidx DrawerLayout -> 宿主真 DrawerLayout（原生
    // 手势/动画/遮罩），其余 androidx 类由 guest 侧 runtime.dex 提供。
    implementation("androidx.drawerlayout:drawerlayout:1.2.0")
}

// target-app-debug.apk -> generated assets（asset 落位；缺则先 :target-app:assembleDebug）
val copyTargetApkAssets = tasks.register<Copy>("copyTargetApkAssets") {
    dependsOn(":target-app:assembleDebug")
    from(project(":target-app").layout.buildDirectory.dir("outputs/apk/debug"))
    include("target-app-debug.apk")
    into(layout.buildDirectory.dir("generated/targetAssets"))
}

// runtime.dex -> generated assets（:runtime-libs:dexRuntimeSdk 产出；恒新鲜）
val copyRuntimeDexAssets = tasks.register<Copy>("copyRuntimeDexAssets") {
    dependsOn(":runtime-libs:dexRuntimeSdk")
    from(project(":runtime-libs").layout.buildDirectory.dir("runtime-dex"))
    include("runtime*.dex")
    into(layout.buildDirectory.dir("generated/runtimeAssets"))
}

tasks.named("preBuild") { dependsOn(copyTargetApkAssets, copyRuntimeDexAssets) }
