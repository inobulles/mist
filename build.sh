#!/bin/sh
set -e

# TODO I would like an Android class or something for Bob the Builder. That would be pretty cool.

# This is the only bit of configuration that needs to be set.
# In the end, I want to use sdkmanager such that we can automatically install and manage the appropriate SDK/NDK versions here.

ANDROID_SDK=/home/obiwac/Android/Sdk
SDK_VERSION=36.1.0 # TODO Is this SDK version or build tools version?
NDK_VESRION=27.2.12479018
ANDROID_VERSION=31
HOST_TRIPLE=linux-x86_64
TARGET_TRIPLE=aarch64-linux-android$ANDROID_VERSION
ABI=arm64-v8a

# Create directories.

mkdir -p .out
rm -r .out/apk_stage
mkdir -p .out/apk_stage
mkdir -p .out/apk_stage/lib/$ABI

# Some useful paths.

BUILD_TOOLS_PATH=$ANDROID_SDK/build-tools/$SDK_VERSION
NDK_PATH=$ANDROID_SDK/ndk/$NDK_VESRION
TOOLCHAIN_PATH=$NDK_PATH/toolchains/llvm/prebuilt/$HOST_TRIPLE
PLATFORM_PATH=$ANDROID_SDK/platforms/android-$ANDROID_VERSION/
NATIVE_APP_GLUE_PATH=$NDK_PATH/sources/android/native_app_glue

# Some commands.

CC=$TOOLCHAIN_PATH/bin/$TARGET_TRIPLE-clang
CXX=$TOOLCHAIN_PATH/bin/$TARGET_TRIPLE-clang++
AR=$TOOLCHAIN_PATH/bin/llvm-ar
AAPT=$BUILD_TOOLS_PATH/aapt
APKSIGNER=$BUILD_TOOLS_PATH/apksigner
ZIPALIGN=$BUILD_TOOLS_PATH/zipalign

# Build the native app glue static library from the NDK.

$CC -Wall -Werror -c $NATIVE_APP_GLUE_PATH/android_native_app_glue.c -o .out/native_app_glue.o
$AR rcs .out/libnative_app_glue.a .out/native_app_glue.o

# Copy over libc++_shared from NDK.

cp $TOOLCHAIN_PATH/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so .out/apk_stage/lib/arm64-v8a

# Build the program itself.
# This is simply a shared library loaded presumably somewhere in the JVM.

$CXX \
	-Wall \
	-I $NATIVE_APP_GLUE_PATH --sysroot=$TOOLCHAIN_PATH/sysroot \
	-fPIC \
	-c main.cpp -o .out/main.o

$CXX \
	-I $NATIVE_APP_GLUE_PATH -L.out -shared \
	.out/main.o -o .out/apk_stage/lib/$ABI/libmain.so \
	-llog -landroid -lEGL -lGLESv1_CM .out/native_app_glue.o

# Generate the APK.
# keytool comes from jdk21-openjdk on Arch.

keytool \
	-genkeypair \
	-validity 1000 -keyalg RSA \
	-dname "CN=Inobulles,O=Android,C=BE" \
	-keystore .out/debug.keystore \
	-storepass 123456 -keypass 123456 \
	-alias MistKey || true

$AAPT package -f -M AndroidManifest.xml -I $PLATFORM_PATH/android.jar -F .out/Mist.apk .out/apk_stage
# jarsigner -keystore .out/debug.keystore -storepass 123456 -keypass 123456 -signedjar .out/Mist.signed.apk .out/Mist.unsigned.apk MistKey
$APKSIGNER sign --ks .out/debug.keystore --ks-pass pass:123456 .out/Mist.apk
