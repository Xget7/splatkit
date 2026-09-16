#!/usr/bin/env bash
# Formats and lints every C++ source of the repository with the clang tools the NDK
# ships, so the machine that builds the library is the machine that lints it.
#
#   scripts/lint-cpp.sh          check: exit 1 on a formatting or clang-tidy finding
#   scripts/lint-cpp.sh --fix    rewrite the formatting and apply clang-tidy's fixes
#
# Needs the NDK in ANDROID_NDK_HOME, or under ANDROID_HOME/ndk, or in the default SDK
# location; CMake, and the network on the first run (FetchContent).
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
fix=0
[[ "${1:-}" == "--fix" ]] && fix=1

ndk_version="27.1.12297006"
find_ndk() {
  if [[ -n "${ANDROID_NDK_HOME:-}" ]]; then echo "$ANDROID_NDK_HOME"; return; fi
  for sdk in "${ANDROID_HOME:-}" "${ANDROID_SDK_ROOT:-}" "$HOME/Library/Android/sdk" "$HOME/Android/Sdk"; do
    [[ -n "$sdk" && -d "$sdk/ndk/$ndk_version" ]] && { echo "$sdk/ndk/$ndk_version"; return; }
  done
  echo "NDK $ndk_version not found; set ANDROID_NDK_HOME" >&2
  exit 2
}
ndk="$(find_ndk)"
case "$(uname -s)" in
  Darwin) host=darwin-x86_64 ;;
  Linux) host=linux-x86_64 ;;
  *) echo "unsupported host" >&2; exit 2 ;;
esac
bin="$ndk/toolchains/llvm/prebuilt/$host/bin"
clang_format="$bin/clang-format"
clang_tidy="$bin/clang-tidy"

core="$root/packages/splat-core"
engine="$root/packages/splatkit-engine"
android="$root/packages/splatkit-android/src/main/cpp"
ios="$root/packages/splatkit-ios/Sources/SplatKitCore"
build="$root/build/lint"

# The iOS sources are Objective-C++: clang-format handles them, the NDK's clang-tidy
# cannot parse them against an Apple SDK, so they are formatted here and built, with
# warnings as errors, by scripts/build-ios.sh.
sources() {
  find "$core/include" "$core/src" "$core/tests" "$core/tools" \
    "$engine/include" "$engine/src" "$engine/tests" "$android" "$ios" \
    -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.mm' \) | sort
}

echo "clang-format"
if (( fix )); then
  sources | xargs "$clang_format" -i
else
  sources | xargs "$clang_format" --dry-run --Werror
fi

# Both compile databases target Android, tests and tools included: the NDK's clang-tidy
# then parses everything against the NDK's own libc++, whatever the host's SDK ships.
toolchain=(
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
  -DCMAKE_TOOLCHAIN_FILE="$ndk/build/cmake/android.toolchain.cmake"
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 -DANDROID_STL=c++_static
)
echo "configure splat-core"
cmake -S "$core" -B "$build/core" "${toolchain[@]}" \
  -DSPLAT_CORE_BUILD_TESTS=ON -DSPLAT_CORE_BUILD_TOOLS=ON > /dev/null

echo "configure splatkit-engine"
cmake -S "$engine" -B "$build/engine" "${toolchain[@]}" \
  -DSPLATKIT_ENGINE_BUILD_TESTS=ON > /dev/null

echo "configure splatkit-android"
cmake -S "$android" -B "$build/android" "${toolchain[@]}" \
  -DSPLATKIT_ANDROID_BUILD_TESTS=ON > /dev/null
# The splat pipeline includes the generated shader headers.
cmake --build "$build/android" --target splatkit_shaders_generate > /dev/null

failed=0
tidy() {  # <compile database dir> <sources...>
  local db="$1"; shift
  local extra=()
  (( fix )) && extra=(--fix --fix-errors)
  printf '%s\n' "$@" | xargs -P "$(getconf _NPROCESSORS_ONLN)" -n 4 \
    "$clang_tidy" -p "$db" --quiet "${extra[@]}" || failed=1
}

echo "clang-tidy splat-core"
tidy "$build/core" $(find "$core/src" "$core/tests" "$core/tools" -name '*.cpp' | sort)
echo "clang-tidy splatkit-engine"
tidy "$build/engine" $(find "$engine/src" "$engine/tests" -name '*.cpp' | sort)
echo "clang-tidy splatkit-android"
tidy "$build/android" $(find "$android" -name '*.cpp' | sort)
# clang-tidy's fixes do not keep the formatting.
(( fix )) && sources | xargs "$clang_format" -i
if (( failed )); then
  echo "clang-tidy found problems; scripts/lint-cpp.sh --fix applies the ones it can"
  exit 1
fi
echo "clean"
