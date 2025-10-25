#!/bin/bash
set -e

# XXX This script is very hacky and often just doesn't work.

adb shell am start -n com.inobulles.mist/android.app.NativeActivity
sleep 0.5
PID=$(adb shell ps | grep com.inobulles.mist | tr -s [:space:] ' ' | cut -d' ' -f2)
echo $PID
adb logcat | grep -F $PID
