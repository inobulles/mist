#!/bin/sh
set -e

# TODO I would like an Android class or something for Bob the Builder. That would be pretty cool.
# This script also works on FreeBSD through the Linuxulator (I know :o).

. ./config.sh

# If on FreeBSD, make sure linux64 is loaded.

if [ "$(uname)" = "FreeBSD" ] && [ "$(kldstat | grep linux64)" = "" ]; then
	doas kldload linux64
fi

# Create directories.

mkdir -p .out
rm -r .out/apk_stage
mkdir -p .out/apk_stage
mkdir -p .out/apk_stage/lib/$ABI

# Build the native app glue static library from the NDK.

$CC -Wall -Werror -c $NATIVE_APP_GLUE_PATH/android_native_app_glue.c -o .out/native_app_glue.o
$AR rcs .out/libnative_app_glue.a .out/native_app_glue.o

# Copy the OpenXR loader to the staging area.

cp $OPENXR_SDK/build/src/loader/libopenxr_loader.so .out/apk_stage/lib/$ABI

# Copy the OpenXR API layers to the staging area.

cp $OPENXR_SDK/build/src/api_layers/libXrApiLayer_* .out/apk_stage/lib/$ABI
cp $OPENXR_SDK/build/src/api_layers/XrApiLayer_*.json assets/openxr/1/api_layers/explicit.d

# Copy over libc++_shared from NDK.

cp $TOOLCHAIN_PATH/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so .out/apk_stage/lib/$ABI

# Build the program itself.
# This is simply a shared library loaded presumably somewhere in the JVM.

echo "Build objs."

objs=

for src in gvd env shader win desktop; do
	$CC \
		-Wall \
		-I$NATIVE_APP_GLUE_PATH -I$OPENXR_SDK/build/include -Isrc/glad/include \
		--sysroot=$TOOLCHAIN_PATH/sysroot \
		-fPIC \
		-c src/$src.c -o .out/$src.o &

	objs="$objs .out/$src.o"
done

if [ ! -f .out/glad.o ]; then
	$CC -Isrc/glad/include -fPIC -c src/glad/src/gles2.c -o .out/glad.o &
fi

objs="$objs .out/glad.o"

$CXX \
	-Wall \
	-I$NATIVE_APP_GLUE_PATH -I$OPENXR_SDK/build/include -Isrc/glad/include \
	--sysroot=$TOOLCHAIN_PATH/sysroot \
	-fPIC \
	-c src/main.cpp -o .out/main.o &

objs="$objs .out/main.o"
wait

echo "Link."

$CXX \
	-I $NATIVE_APP_GLUE_PATH -L.out/apk_stage/lib/$ABI -shared \
	$objs -o .out/apk_stage/lib/$ABI/libmain.so \
	-llog -lopenxr_loader -landroid -lEGL .out/native_app_glue.o

echo "Install AQUA stuff."

export CC
export AR
export BOB_TARGET=arm64-android

bob -p assets -C $AQUA/gv install
bob -p assets -C $AQUA/vdev/vr install

echo "Generate the APK."

# keytool comes from jdk21-openjdk on Arch.

if [ ! -f .out/debug.keystore ]; then
	keytool \
		-genkeypair \
		-validity 1000 -keyalg RSA \
		-dname "CN=Inobulles,O=Android,C=BE" \
		-keystore .out/debug.keystore \
		-storepass 123456 -keypass 123456 \
		-alias MistKey || true
fi

# This LD_PRELOAD exists for when running on FreeBSD.
# TODO I think we can drastically speed up AAPT2 by modifying an existing APK instead of creating a new one each time.

LD_PRELOAD=$BUILD_TOOLS_PATH/lib64/libc++.so $AAPT package -f \
	-M AndroidManifest.xml -I $PLATFORM_PATH/android.jar -A assets \
	-F .out/Mist.apk .out/apk_stage

echo "Sign the APK."

$APKSIGNER sign --ks .out/debug.keystore --ks-pass pass:123456 .out/Mist.apk

echo "Built!"
