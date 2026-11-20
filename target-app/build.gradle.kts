// target-app — v2 顶层测试 app 模块（旧 guest 考卷模块继任，v1 target-apk 归位）。
// 三方库版本内联自 tests/target-apk/build-shell/gradle/libs.versions.toml 的已验证组合
// （AGP 9.3.0 / Gradle 9.6 / JDK 25 实证），不给 v2 根加版本目录，保持测试 app 模块同款直坐标风格。
plugins {
    id("com.android.application")
}

android {
    namespace = "com.coke.target"
    compileSdk {
        version = release(37)
    }

    defaultConfig {
        applicationId = "com.coke.target"
        minSdk = 26
        targetSdk = 37
        versionCode = 1
        versionName = "1.0"

        ndk {
            abiFilters += listOf("x86_64", "arm64-v8a")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_11
        targetCompatibility = JavaVersion.VERSION_11
    }
    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }
    packaging {
        resources.excludes += setOf(
            "META-INF/INDEX.LIST",
            "META-INF/io.netty.versions.properties",
        )
    }
}

dependencies {
    implementation("androidx.constraintlayout:constraintlayout:2.0.4")
    implementation("androidx.drawerlayout:drawerlayout:1.2.0")
    implementation("org.apache.commons:commons-lang3:3.17.0")
    implementation("com.alibaba:fastjson:1.2.83")
    implementation("com.alibaba.fastjson2:fastjson2:2.0.66")
    implementation("com.google.code.gson:gson:2.15.0")
    implementation("com.fasterxml.jackson.core:jackson-databind:2.23.0")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-core:1.11.0")
    implementation("io.netty:netty-buffer:4.1.118.Final")
    implementation("io.netty:netty-codec:4.1.118.Final")
    implementation("io.netty:netty-codec-http:4.1.118.Final")
    implementation("io.netty:netty-handler:4.1.118.Final")
    implementation("io.netty:netty-transport:4.1.118.Final")
    implementation("com.squareup.okio:okio:3.18.2")
    implementation("com.squareup.okhttp3:okhttp:4.12.0")
    implementation("io.reactivex.rxjava3:rxjava:3.1.12")
    implementation("org.slf4j:slf4j-api:2.0.20")
}
