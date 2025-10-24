#!/bin/sh
set -e

. ./config.sh

# Build the OpenXR loader library.

pushd $OPENXR_SDK
mkdir -p build
cd build

cmake \
	-DCMAKE_TOOLCHAIN_FILE=$NDK_PATH/build/cmake/android.toolchain.cmake \
	-DANDROID_ABI=$ABI \
	-DANDROID_PLATFORM=$PLATFORM \
	-DBUILD_LOADER=ON \
	-DBUILD_API_LAYERS=OFF \
	-DBUILD_CONFORMANCE_TESTS=OFF \
	-DBUILD_WITH_ANDROID=ON \
	..

make -j$(nproc) openxr_loader
popd
