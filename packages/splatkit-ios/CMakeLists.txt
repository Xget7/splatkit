cmake_minimum_required(VERSION 3.22)
project(splatkit_ios LANGUAGES C CXX OBJCXX)

# Configure for a device with the iOS toolchain flags, e.g.
#   cmake -S packages/splatkit-ios -B build/ios -G Xcode -DCMAKE_SYSTEM_NAME=iOS \
#         -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=17.0
# scripts/build-ios.sh does exactly that and leaves the static libraries where the
# dev app and the Swift package pick them up.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(SPLATKIT_ENGINE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../splatkit-engine splatkit-engine)

# The Metal shader source travels inside the library as a string and is compiled by the
# device at start, so a host app needs no metallib of ours in its bundle.
include(cmake/embed.cmake)
splatkit_embed_text(splatkit_ios_shaders
  ${CMAKE_CURRENT_SOURCE_DIR}/Sources/SplatKitCore/rendering/Splat.metal
  SplatShaderSource
)

add_library(splatkit_ios STATIC
  Sources/SplatKitCore/rendering/MetalSplatRenderer.mm
  Sources/SplatKitCore/engine/SKSplatEngine.mm
)
target_include_directories(splatkit_ios
  PUBLIC Sources/SplatKitCore/include
  PRIVATE Sources/SplatKitCore ${CMAKE_CURRENT_BINARY_DIR}/embedded
)
target_compile_options(splatkit_ios PRIVATE -Wall -Wextra -Werror -fobjc-arc)
target_link_libraries(splatkit_ios PUBLIC splatkit_engine
  "-framework Metal" "-framework QuartzCore" "-framework Foundation"
  "-framework CoreGraphics" "-framework ImageIO" "-framework UniformTypeIdentifiers")
add_dependencies(splatkit_ios splatkit_ios_shaders)
