# This is the only bit of configuration that needs to be set.
# In the end, I want to use sdkmanager such that we can automatically install and manage the appropriate SDK/NDK versions here.

ANDROID_SDK=/home/obiwac/Android/Sdk
OPENXR_SDK=/home/obiwac/aqua/vr/OpenXR-SDK-Source # From https://github.com/KhronosGroup/OpenXR-SDK-Source.
SDK_VERSION=36.1.0 # TODO Is this SDK version or build tools version?
NDK_VERSION=27.2.12479018
ANDROID_VERSION=31
PLATFORM=android-$ANDROID_VERSION
HOST_TRIPLE=linux-x86_64
TARGET_TRIPLE=aarch64-linux-android$ANDROID_VERSION
ABI=arm64-v8a

# Some useful paths.

BUILD_TOOLS_PATH=$ANDROID_SDK/build-tools/$SDK_VERSION
NDK_PATH=$ANDROID_SDK/ndk/$NDK_VERSION
TOOLCHAIN_PATH=$NDK_PATH/toolchains/llvm/prebuilt/$HOST_TRIPLE
PLATFORM_PATH=$ANDROID_SDK/platforms/android-$ANDROID_VERSION/
NATIVE_APP_GLUE_PATH=$NDK_PATH/sources/android/native_app_glue
