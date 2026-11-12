#!/usr/bin/env bash
# Adds the native sound engine to the Android project that `npx cap add android` generates.
set -euo pipefail
APP=android/app
PKG=$APP/src/main/java/com/contactvirgilio/melasa
mkdir -p "$PKG" $APP/src/main/cpp
cp android-native/java/*.java "$PKG/"
cp android-native/cpp/* $APP/src/main/cpp/
cat >> $APP/build.gradle <<'GRADLE'

// --- Melasa native sound engine (Oboe) ---
android {
    buildFeatures { prefab true }
    defaultConfig {
        externalNativeBuild { cmake { arguments "-DANDROID_STL=c++_shared" } }
        ndk { abiFilters "arm64-v8a", "armeabi-v7a", "x86_64" }
    }
    externalNativeBuild { cmake { path file("src/main/cpp/CMakeLists.txt") } }
}
dependencies {
    implementation "com.google.oboe:oboe:1.9.3"
}
GRADLE
