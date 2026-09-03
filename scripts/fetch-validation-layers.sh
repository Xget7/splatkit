#!/usr/bin/env bash
# Downloads the Khronos Vulkan validation layer for Android into the dev app's debug
# jniLibs (gitignored). The Debug build of the engine enables the layer when present and
# routes its messages to logcat under the SplatKit tag.
set -euo pipefail
VERSION="${1:-1.4.357.0}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$ROOT/apps/android-dev/app/src/debug/jniLibs"
TMP="$(mktemp -d)"
URL="https://github.com/KhronosGroup/Vulkan-ValidationLayers/releases/download/vulkan-sdk-$VERSION/android-binaries-$VERSION.zip"
echo "fetching $URL"
curl -sSL -o "$TMP/layers.zip" "$URL"
unzip -q -o "$TMP/layers.zip" -d "$TMP/layers"
mkdir -p "$DEST/arm64-v8a"
find "$TMP/layers" -path '*arm64-v8a*' -name 'libVkLayer_khronos_validation.so' -exec cp {} "$DEST/arm64-v8a/" \;
ls -la "$DEST/arm64-v8a"
rm -rf "$TMP"
