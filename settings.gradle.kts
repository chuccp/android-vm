pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}

dependencyResolutionManagement {
    // runtime-libs（旧版资产）可能自带仓库声明，这里以 settings 仓库优先
    repositoriesMode.set(RepositoriesMode.PREFER_SETTINGS)
    repositories {
        google()
        mavenCentral()
    }
}

rootProject.name = "android-vm"

include(":app")
include(":runtime-libs")
include(":target-app")
