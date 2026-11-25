#!/bin/bash
# SPDX-License-Identifier: AGPL-3.0-only
# The APK, out of libmain.so (cmake/android.cmake) and the activity's classes.dex (written by the
# SDL fork's dex writer) – experimental, docs/SDL.md "Android".  After progressive-chat's
# cmake/android-apk.cmake and tools/android_apk.py (the same author): the resources linked
# against the platform's own (here: the SDK's android.jar), the classes and the libraries put
# in, the archive lined up and signed with a key made for it.  Run in CI (.github/workflows/android.yml).
#
#   tools/android/apk.sh --out fahrsim2.apk --dex classes.dex --lib arm64-v8a:build/libmain.so [--lib …]
#                        [--version-name 2.1.0-dev] [--version-code 1] [--keystore debug.p12]
#
# Needs: ANDROID_HOME (build-tools with aapt2, zipalign, apksigner; a platform's android.jar),
# keytool, zip, and llvm-strip (STRIP=…; the NDK's).
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
out="" dex="" libs=() versionName="2.1.0-dev" versionCode=1 keystore=""
minSdk=26 targetSdk=35
while [ $# -gt 0 ]; do
  case "$1" in
    --out) out="$2"; shift 2 ;;
    --dex) dex="$2"; shift 2 ;;
    --lib) libs+=("$2"); shift 2 ;;
    --version-name) versionName="$2"; shift 2 ;;
    --version-code) versionCode="$2"; shift 2 ;;
    --keystore) keystore="$2"; shift 2 ;;
    *) echo "apk.sh: what is $1?" >&2; exit 2 ;;
  esac
done
[ -n "$out" ] && [ -n "$dex" ] && [ ${#libs[@]} -gt 0 ] || { echo "apk.sh: --out, --dex and --lib are needed" >&2; exit 2; }
: "${ANDROID_HOME:?ANDROID_HOME names the Android SDK}"
tools="$(ls -d "$ANDROID_HOME"/build-tools/* | sort -V | tail -1)"
# (the newest released platform: not a preview such as android-37.2-beta3)
platform="$(ls -d "$ANDROID_HOME"/platforms/android-* | grep -E '/android-[0-9]+$' | sort -V | tail -1)"
echo "apk.sh: build-tools $(basename "$tools"), $(basename "$platform")"
STRIP="${STRIP:-llvm-strip}"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

# the assets the game reads through SDL (a relative path is an asset on Android): the site's
# files (web/ – signs, parts, lang, data …) and the canvas's fonts
mkdir -p "$work/assets"
cp -r "$root/web" "$work/assets/web"
mkdir -p "$work/assets/fonts"
cp "$root"/third_party/fonts/*.ttf "$root"/third_party/fonts/OFL-*.txt "$work/assets/fonts/"

# the resources (the icon), compiled, and linked with the manifest against the platform's own
"$tools/aapt2" compile --dir "$root/android/res" -o "$work/res.zip"
"$tools/aapt2" link -o "$work/base.apk" --manifest "$root/android/AndroidManifest.xml" -I "$platform/android.jar" \
  --min-sdk-version "$minSdk" --target-sdk-version "$targetSdk" --version-code "$versionCode" --version-name "$versionName" \
  -A "$work/assets" "$work/res.zip"

# the classes and the libraries (stripped: what a symbolizer reads stays in the build)
mkdir -p "$work/stage"
cp "$dex" "$work/stage/classes.dex"
for one in "${libs[@]}"; do
  abi="${one%%:*}" lib="${one#*:}"
  mkdir -p "$work/stage/lib/$abi"
  "$STRIP" --strip-unneeded -o "$work/stage/lib/$abi/libmain.so" "$lib"
done
(cd "$work/stage" && zip -q -r "$work/base.apk" classes.dex lib)

# lined up (and the libraries on 16 KB pages), then signed
"$tools/zipalign" -P 16 -f 4 "$work/base.apk" "$work/aligned.apk"
if [ -z "$keystore" ]; then
  keystore="$work/debug.p12"
  keytool -genkeypair -keystore "$keystore" -storetype PKCS12 -alias androiddebugkey -storepass android -keypass android \
    -keyalg RSA -keysize 2048 -validity 10950 -dname "CN=Fahrsim test key, O=fahrsim build, C=XX" > /dev/null
fi
"$tools/apksigner" sign --ks "$keystore" --ks-type PKCS12 --ks-key-alias androiddebugkey --ks-pass pass:android --key-pass pass:android \
  --min-sdk-version "$minSdk" --out "$out" "$work/aligned.apk"
"$tools/apksigner" verify --min-sdk-version "$minSdk" "$out"
echo "apk.sh: $out ($(du -h "$out" | cut -f1))"
