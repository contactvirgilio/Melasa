#!/usr/bin/env bash
# Emulator smoke test: the app must launch, open the low-latency stream and decode the default kit.
set -uo pipefail
adb install -r android/app/build/outputs/apk/debug/app-debug.apk
adb logcat -c
adb shell am start -W -n com.contactvirgilio.melasa/.MainActivity
sleep 30
adb logcat -d -s MelasaAudio:V > smoke.log
cat smoke.log
adb shell pidof com.contactvirgilio.melasa || { echo "FAIL: app is not running"; exit 1; }
grep -q "stream open" smoke.log || { echo "FAIL: audio stream did not open"; exit 1; }
grep -q "loaded 50/50" smoke.log || { echo "FAIL: default kit did not decode"; exit 1; }
echo "SMOKE OK"
