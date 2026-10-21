#!/usr/bin/env bash
# Puts the GPU specifications in build/gpu-refs (gitignored) for grep during GPU work:
#   metal   Apple's Metal Shading Language specification and Feature Set Tables, as PDF plus
#           UTF-8 text. Apple's license allows personal copies only.
#   vulkan  The Khronos Vulkan-Docs sources (spec chapters with their VUIDs, xml/vk.xml) at the
#           tag matching VK_HEADER_VERSION in the NDK's vulkan_core.h, so the spec agrees with
#           the header that compiles. Apache-2.0 and CC-BY-4.0.
# docs/gpu-refs holds the cited facts we commit. Usage: fetch-gpu-refs.sh [metal|vulkan] (both
# when omitted).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

fetch_metal() {
  local dest="$ROOT/build/gpu-refs/metal"
  mkdir -p "$dest"
  for name in Metal-Shading-Language-Specification Metal-Feature-Set-Tables; do
    local url="https://developer.apple.com/metal/$name.pdf"
    echo "fetching $url"
    curl -sSfL -o "$dest/$name.pdf" -D "$dest/$name.headers" "$url"
    grep -i '^last-modified' "$dest/$name.headers" || true
    pdftotext "$dest/$name.pdf" - | iconv -f latin1 -t utf-8 > "$dest/$name.txt"
  done
  ls -la "$dest"
}

fetch_vulkan() {
  local ndk_version sdk header version dest
  ndk_version="$(sed -n 's/.*ndkVersion = "\([^"]*\)".*/\1/p' "$ROOT/packages/splatkit-android/build.gradle.kts")"
  for sdk in "${ANDROID_HOME:-}" "${ANDROID_SDK_ROOT:-}" "$HOME/Library/Android/sdk" "$HOME/Android/Sdk"; do
    [[ -n "$sdk" ]] || continue
    header="$(find "$sdk/ndk/$ndk_version/toolchains/llvm/prebuilt" -path '*/sysroot/usr/include/vulkan/vulkan_core.h' 2>/dev/null | head -1)"
    [[ -n "$header" ]] && break
  done
  if [[ -z "${header:-}" ]]; then
    echo "no vulkan_core.h in NDK $ndk_version" >&2
    exit 1
  fi
  version="$(sed -n 's/^#define VK_HEADER_VERSION \([0-9]*\)$/\1/p' "$header")"
  local major minor
  major="$(sed -n 's/^#define VK_HEADER_VERSION_COMPLETE VK_MAKE_API_VERSION(0, \([0-9]*\), \([0-9]*\), VK_HEADER_VERSION)$/\1/p' "$header")"
  minor="$(sed -n 's/^#define VK_HEADER_VERSION_COMPLETE VK_MAKE_API_VERSION(0, \([0-9]*\), \([0-9]*\), VK_HEADER_VERSION)$/\2/p' "$header")"
  local tag="v$major.$minor.$version"
  dest="$ROOT/build/gpu-refs/vulkan/Vulkan-Docs"
  echo "fetching Vulkan-Docs $tag for $header"
  rm -rf "$dest"
  git -c advice.detachedHead=false clone --quiet --depth 1 --branch "$tag" https://github.com/KhronosGroup/Vulkan-Docs.git "$dest"
  echo "$tag" > "$ROOT/build/gpu-refs/vulkan/TAG"
  du -sh "$dest"
}

case "${1:-all}" in
  metal) fetch_metal ;;
  vulkan) fetch_vulkan ;;
  all) fetch_metal; fetch_vulkan ;;
  *) echo "usage: $0 [metal|vulkan]" >&2; exit 2 ;;
esac
