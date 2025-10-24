#!/bin/sh
set -e

# TODO I would like an Android class or something for Bob the Builder. That would be pretty cool.

. ./config.sh

# Create directories.

mkdir -p .out
rm -r .out/apk_stage
mkdir -p .out/apk_stage
mkdir -p .out/apk_stage/lib/$ABI

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

# Copy the OpenXR loader to the staging area.

cp $OPENXR_SDK/build/src/loader/libopenxr_loader.so .out/apk_stage/lib/$ABI

# Copy over libc++_shared from NDK.

cp $TOOLCHAIN_PATH/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so .out/apk_stage/lib/$ABI

# Build the program itself.
# This is simply a shared library loaded presumably somewhere in the JVM.

intercept-build $CXX \
	-Wall \
	-I $NATIVE_APP_GLUE_PATH -I $OPENXR_SDK/build/include \
	--sysroot=$TOOLCHAIN_PATH/sysroot \
	-fPIC \
	-c main.cpp -o .out/main.o

$CXX \
	-I $NATIVE_APP_GLUE_PATH -L.out/apk_stage/lib/$ABI -shared \
	.out/main.o -o .out/apk_stage/lib/$ABI/libmain.so \
	-llog -lopenxr_loader -landroid -lEGL -lGLESv1_CM .out/native_app_glue.o

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
$APKSIGNER sign --ks .out/debug.keystore --ks-pass pass:123456 .out/Mist.apk
