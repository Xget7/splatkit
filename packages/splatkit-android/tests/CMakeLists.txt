# Included by src/main/cpp/CMakeLists.txt, which sets SPLATKIT_ANDROID_CPP_DIR, vma_include and vkb_include.
set(vulkan_source_dir ${SPLATKIT_ANDROID_CPP_DIR}/rendering/vulkan)

add_library(splatkit_vulkan_test_support STATIC
  ${vulkan_source_dir}/device/VulkanContext.cpp
  ${vulkan_source_dir}/device/VulkanBuffer.cpp
  ${vulkan_source_dir}/device/Vma.cpp
)
target_include_directories(splatkit_vulkan_test_support PUBLIC ${SPLATKIT_ANDROID_CPP_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(splatkit_vulkan_test_support SYSTEM PUBLIC ${vma_include} ${vkb_include})
target_compile_definitions(splatkit_vulkan_test_support PUBLIC VK_USE_PLATFORM_ANDROID_KHR)
target_compile_options(splatkit_vulkan_test_support PRIVATE
  -Wall -Wextra -Werror -Wno-unused-parameter -Wno-missing-field-initializers)
target_link_libraries(splatkit_vulkan_test_support PUBLIC
  splatkit_engine vk-bootstrap::vk-bootstrap GPUOpen::VulkanMemoryAllocator vulkan android log)

add_executable(splatkit_gpu_buffer_test rendering/vulkan/device/VulkanBufferTest.cpp)
target_link_libraries(splatkit_gpu_buffer_test PRIVATE splatkit_vulkan_test_support)
add_executable(splatkit_gpu_upload_pressure_test rendering/vulkan/device/VulkanUploadPressureTest.cpp)
target_link_libraries(splatkit_gpu_upload_pressure_test PRIVATE splatkit_vulkan_test_support)
add_executable(splatkit_lod_selection_test rendering/vulkan/selection/VulkanLodSelectionTest.cpp
  ${vulkan_source_dir}/selection/VulkanLodSelection.cpp)
target_link_libraries(splatkit_lod_selection_test PRIVATE
  splatkit_vulkan_test_support splatkit_shaders)
add_executable(splatkit_visibility_test rendering/vulkan/cull/VulkanVisibilityPassTest.cpp
  ${vulkan_source_dir}/cull/VulkanVisibilityPass.cpp)
target_link_libraries(splatkit_visibility_test PRIVATE
  splatkit_vulkan_test_support splatkit_shaders)
add_executable(splatkit_vulkan_frame_test rendering/vulkan/frame/VulkanFrameComputeTest.cpp
  ${vulkan_source_dir}/frame/VulkanFrameCompute.cpp ${vulkan_source_dir}/selection/VulkanLodSelection.cpp
  ${vulkan_source_dir}/cull/VulkanVisibilityPass.cpp ${vulkan_source_dir}/sort/VulkanRadixSort.cpp)
target_link_libraries(splatkit_vulkan_frame_test PRIVATE
  splatkit_vulkan_test_support splatkit_shaders)
add_executable(splatkit_radix_sort_test rendering/vulkan/sort/VulkanRadixSortTest.cpp
  ${vulkan_source_dir}/sort/VulkanRadixSort.cpp)
target_link_libraries(splatkit_radix_sort_test PRIVATE
  splatkit_vulkan_test_support splatkit_shaders)
