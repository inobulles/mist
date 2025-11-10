#!/bin/sh
set -e

# Danke https://stackoverflow.com/questions/39569208/stream-android-screen-to-video-player.

(
	while [ 1 ]; do adb shell exit; done
) &

# --bit-rate=400000
# --size=

adb exec-out screenrecord --time-limit=180 --output-format=h264 - | mpv --vf="crop=3*iw/8:2*ih/3:iw/16:ih/6" -
