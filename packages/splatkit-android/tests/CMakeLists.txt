# Included by src/main/cpp/CMakeLists.txt, which sets SPLATKIT_ANDROID_CPP_DIR, vma_include and vkb_include.
set(sources ${SPLATKIT_ANDROID_CPP_DIR}/rendering/vulkan)

add_library(splatkit_vulkan_test_support STATIC
  ${sources}/VulkanContext.cpp
  ${sources}/GpuBuffer.cpp
  ${sources}/Vma.cpp
)
target_include_directories(splatkit_vulkan_test_support PUBLIC ${SPLATKIT_ANDROID_CPP_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(splatkit_vulkan_test_support SYSTEM PUBLIC ${vma_include} ${vkb_include})
target_compile_definitions(splatkit_vulkan_test_support PUBLIC VK_USE_PLATFORM_ANDROID_KHR)
target_compile_options(splatkit_vulkan_test_support PRIVATE
  -Wall -Wextra -Werror -Wno-unused-parameter -Wno-missing-field-initializers)
target_link_libraries(splatkit_vulkan_test_support PUBLIC
  splatkit_engine vk-bootstrap::vk-bootstrap GPUOpen::VulkanMemoryAllocator vulkan android log)

add_executable(splatkit_gpu_buffer_test rendering/vulkan/device/GpuBufferTest.cpp)
target_link_libraries(splatkit_gpu_buffer_test PRIVATE splatkit_vulkan_test_support)
add_executable(splatkit_gpu_upload_pressure_test rendering/vulkan/device/GpuUploadPressureTest.cpp)
target_link_libraries(splatkit_gpu_upload_pressure_test PRIVATE splatkit_vulkan_test_support)
add_executable(splatkit_lod_selection_test rendering/vulkan/selection/LodSelectionTest.cpp
  ${sources}/LodSelection.cpp)
target_link_libraries(splatkit_lod_selection_test PRIVATE
  splatkit_vulkan_test_support splatkit_shaders)
add_executable(splatkit_visibility_test rendering/vulkan/cull/VisibilityPassTest.cpp
  ${sources}/VisibilityPass.cpp)
target_link_libraries(splatkit_visibility_test PRIVATE
  splatkit_vulkan_test_support splatkit_shaders)
add_executable(splatkit_vulkan_frame_test rendering/vulkan/frame/VulkanFrameComputeTest.cpp
  ${sources}/VulkanFrameCompute.cpp ${sources}/LodSelection.cpp
  ${sources}/VisibilityPass.cpp ${sources}/RadixSort.cpp)
target_link_libraries(splatkit_vulkan_frame_test PRIVATE
  splatkit_vulkan_test_support splatkit_shaders)
add_executable(splatkit_radix_sort_test rendering/vulkan/sort/RadixSortTest.cpp
  ${sources}/RadixSort.cpp)
target_link_libraries(splatkit_radix_sort_test PRIVATE
  splatkit_vulkan_test_support splatkit_shaders)
