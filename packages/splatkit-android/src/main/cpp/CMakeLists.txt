cmake_minimum_required(VERSION 3.22)
project(splatkit LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# Everything links into the shared libsplatkit.so, fetched static libraries included.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(FetchContent)

# vk-bootstrap: instance, device and swapchain creation without the boilerplate (MIT).
# Its own option overrides CMAKE_POSITION_INDEPENDENT_CODE, so set it explicitly.
set(VK_BOOTSTRAP_POSITION_INDEPENDENT_CODE ON CACHE BOOL "" FORCE)
FetchContent_Declare(
  vk-bootstrap
  GIT_REPOSITORY https://github.com/charles-lunarg/vk-bootstrap.git
  GIT_TAG v1.4.307
)
# VulkanMemoryAllocator: buffer and image memory (MIT).
FetchContent_Declare(
  VulkanMemoryAllocator
  GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
  GIT_TAG v3.2.1
)
FetchContent_MakeAvailable(vk-bootstrap VulkanMemoryAllocator)

set(SPLATKIT_ENGINE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../../splatkit-engine splatkit-engine)

include(cmake/shaders.cmake)
splatkit_compile_shaders(splatkit_shaders
  shaders/triangle.vert
  shaders/triangle.frag
  shaders/splat.vert
  shaders/splat.frag
  shaders/visibility.comp
  shaders/prepare_indirect.comp
  shaders/lod_selection.comp
  shaders/radix_prepare.comp
  shaders/radix_histogram.comp
  shaders/radix_scan.comp
  shaders/radix_scatter.comp
)

add_library(splatkit SHARED
  rendering/vulkan/VulkanContext.cpp
  rendering/vulkan/Swapchain.cpp
  rendering/vulkan/RenderTarget.cpp
  rendering/vulkan/FrameLoop.cpp
  rendering/vulkan/DebugTrianglePipeline.cpp
  rendering/vulkan/GpuBuffer.cpp
  rendering/vulkan/SplatPipeline.cpp
  rendering/vulkan/VisibilityPass.cpp
  rendering/vulkan/LodSelection.cpp
  rendering/vulkan/RadixSort.cpp
  rendering/vulkan/VulkanFrameCompute.cpp
  rendering/vulkan/VulkanSplatRenderer.cpp
  rendering/vulkan/Vma.cpp
  engine/AndroidEngine.cpp
  jni/SplatKitJni.cpp
)
target_include_directories(splatkit PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(splatkit PRIVATE VK_USE_PLATFORM_ANDROID_KHR)
# Vulkan structs are initialised as `{VK_STRUCTURE_TYPE_...}`; the rest is zero on purpose.
target_compile_options(splatkit PRIVATE
  -Wall -Wextra -Werror -Wno-unused-parameter -Wno-missing-field-initializers)
# Third party headers are not held to our warning level.
get_target_property(vma_include GPUOpen::VulkanMemoryAllocator INTERFACE_INCLUDE_DIRECTORIES)
get_target_property(vkb_include vk-bootstrap::vk-bootstrap INTERFACE_INCLUDE_DIRECTORIES)
target_include_directories(splatkit SYSTEM PRIVATE ${vma_include} ${vkb_include})
# Hide everything the JVM does not call. See splatkit.map.
target_compile_options(splatkit PRIVATE -fvisibility=hidden -fvisibility-inlines-hidden)
target_link_options(splatkit PRIVATE
  "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/splatkit.map"
  "-Wl,--exclude-libs,ALL"
)
target_link_libraries(splatkit PRIVATE
  splatkit_engine
  splatkit_shaders
  vk-bootstrap::vk-bootstrap
  GPUOpen::VulkanMemoryAllocator
  vulkan
  android
  log
)

option(SPLATKIT_ANDROID_BUILD_TESTS "Build no-surface Vulkan tests (run through adb)" OFF)
if(SPLATKIT_ANDROID_BUILD_TESTS)
  add_library(splatkit_vulkan_test_support STATIC
    rendering/vulkan/VulkanContext.cpp
    rendering/vulkan/GpuBuffer.cpp
    rendering/vulkan/Vma.cpp
  )
  target_include_directories(splatkit_vulkan_test_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_include_directories(splatkit_vulkan_test_support SYSTEM PUBLIC ${vma_include} ${vkb_include})
  target_compile_definitions(splatkit_vulkan_test_support PUBLIC VK_USE_PLATFORM_ANDROID_KHR)
  target_compile_options(splatkit_vulkan_test_support PRIVATE
    -Wall -Wextra -Werror -Wno-unused-parameter -Wno-missing-field-initializers)
  target_link_libraries(splatkit_vulkan_test_support PUBLIC
    splatkit_engine vk-bootstrap::vk-bootstrap GPUOpen::VulkanMemoryAllocator vulkan android log)
  add_executable(splatkit_gpu_buffer_test tests/GpuBufferTest.cpp)
  target_link_libraries(splatkit_gpu_buffer_test PRIVATE splatkit_vulkan_test_support)
  add_executable(splatkit_gpu_upload_pressure_test tests/GpuUploadPressureTest.cpp)
  target_link_libraries(splatkit_gpu_upload_pressure_test PRIVATE splatkit_vulkan_test_support)
  add_executable(splatkit_lod_selection_test tests/LodSelectionTest.cpp
    rendering/vulkan/LodSelection.cpp)
  target_link_libraries(splatkit_lod_selection_test PRIVATE
    splatkit_vulkan_test_support splatkit_shaders)
  add_executable(splatkit_visibility_test tests/VisibilityPassTest.cpp
    rendering/vulkan/VisibilityPass.cpp)
  target_link_libraries(splatkit_visibility_test PRIVATE
    splatkit_vulkan_test_support splatkit_shaders)
  add_executable(splatkit_vulkan_frame_test tests/VulkanFrameComputeTest.cpp
    rendering/vulkan/VulkanFrameCompute.cpp rendering/vulkan/LodSelection.cpp
    rendering/vulkan/VisibilityPass.cpp rendering/vulkan/RadixSort.cpp)
  target_link_libraries(splatkit_vulkan_frame_test PRIVATE
    splatkit_vulkan_test_support splatkit_shaders)
  add_executable(splatkit_radix_sort_test tests/RadixSortTest.cpp
    rendering/vulkan/RadixSort.cpp)
  target_link_libraries(splatkit_radix_sort_test PRIVATE
    splatkit_vulkan_test_support splatkit_shaders)
endif()
