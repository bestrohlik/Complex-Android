#!/usr/bin/env bash
# Sestaví complex.apk (arm64) z main.cpp.
# Potřebuje: Android SDK + NDK, JDK (keytool) a raylib zkompilovaný pro Android (libraylib.a).
#   export ANDROID_SDK=/cesta/k/android-sdk
#   export ANDROID_NDK=/cesta/k/android-ndk
#   export RAYLIB_SRC=/cesta/k/raylib/src
set -euo pipefail

: "${ANDROID_SDK:?Nastav ANDROID_SDK (cesta k Android SDK)}"
: "${ANDROID_NDK:?Nastav ANDROID_NDK (cesta k Android NDK)}"
: "${RAYLIB_SRC:?Nastav RAYLIB_SRC (raylib/src se zkompilovanou libraylib.a pro Android)}"

API=29
APP=complex
HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"

[ -f "$RAYLIB_SRC/libraylib.a" ] || { echo "CHYBA: $RAYLIB_SRC/libraylib.a neexistuje (nejdriv zkompiluj raylib pro Android)"; exit 1; }

TC="$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64"
CC="$TC/bin/aarch64-linux-android${API}-clang"
CXX="$TC/bin/aarch64-linux-android${API}-clang++"
GLUE="$ANDROID_NDK/sources/android/native_app_glue"
BT="$(ls -d "$ANDROID_SDK"/build-tools/* | sort -V | tail -1)"
JAR="$(ls -d "$ANDROID_SDK"/platforms/android-*/android.jar | sort -V | tail -1)"
echo "build-tools: $BT"
echo "android.jar: $JAR"

rm -rf build
mkdir -p build/obj build/apk/lib/arm64-v8a

echo "== kompilace =="
"$CC" -c "$GLUE/android_native_app_glue.c" -o build/obj/glue.o -fPIC -I"$GLUE"
"$CXX" -c main.cpp -o build/obj/main.o -std=c++17 -O2 -fPIC -DPLATFORM_ANDROID \
    -I"$RAYLIB_SRC" -I"$GLUE" -ffunction-sections -fdata-sections

echo "== linkovani =="
"$CXX" build/obj/main.o build/obj/glue.o -o build/apk/lib/arm64-v8a/libmain.so \
    -shared -static-libstdc++ -u ANativeActivity_onCreate -Wl,--gc-sections -Wl,-z,max-page-size=16384 \
    -L"$RAYLIB_SRC" -lraylib -llog -landroid -lEGL -lGLESv2 -lOpenSLES -latomic -lm -ldl

echo "== balicek APK =="
"$BT/aapt" package -f -M AndroidManifest.xml -I "$JAR" -F build/unaligned.apk \
    --min-sdk-version "$API" --target-sdk-version 33 --version-code 1 --version-name 1.0
( cd build/apk && "$BT/aapt" add ../unaligned.apk lib/arm64-v8a/libmain.so > /dev/null )
"$BT/zipalign" -f -p 4 build/unaligned.apk build/aligned.apk

if [ ! -f build/debug.keystore ]; then
    keytool -genkeypair -keystore build/debug.keystore -storepass android -keypass android \
        -alias key -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=Complex" > /dev/null 2>&1
fi
"$BT/apksigner" sign --ks build/debug.keystore --ks-pass pass:android --key-pass pass:android \
    --out "$APP.apk" build/aligned.apk

echo
echo "HOTOVO: $HERE/$APP.apk"
ls -lh "$APP.apk"
