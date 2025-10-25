# Mist

Prototype for connecting Android-based VR headsets (currently targeting and testing on the original Oculus Quest) to the GrapeVine, which I'm calling "Mist" at the moment.

The VDRIVER code is being developed in the AQUA monorepo.
This repo just contains the code for the Android app, which interacts with the headset through OpenXR and which uses OpenGL ES to render the virtual environment and desktop.

## Building

Gradle makes me sad and I kind of despise Android Studio, so I'm doing this manually in a `build.sh` script.
Eventually I wanna integrate this with the [Bob buildsystem](https://github.com/inobulles/bob), but for now, especially as I'm still shaky about how all this Android stuff works, its easier to prototype like this.

This expects you to have an Android SDK and NDK already installed, which you can either download with Android Studio or (preferably) with [`sdkmanager`](https://developer.android.com/tools/sdkmanager).
You also have to point `build.sh` to the SDK through the `ANDROID_SDK` variable (in `config.sh`).
When I add Android support to Bob the Builder, I will make it integrate with `sdkmanager`.

To build the APK, simply run:

```sh
sh build.sh
```

This command will generate a signed APK in `.out/Mist.apk`.

## Installing & debugging

Installing:

```sh
adb install .out/Mist.apk
```

Debugging (i.e. starting and logging):

```sh
adb shell am start -n com.inobulles.mist/android.app.NativeActivity
adb logcat | grep native-activity
```

## Resources

- Slightly out of date, but helped me with the steps to build APKs outside of Android Studio: <https://github.com/skanti/Android-Manual-Build-Command-Line>
- Way nicer and clearer OpenXR examples than hello_xr: <https://github.com/KhronosGroup/OpenXR-Tutorials>
