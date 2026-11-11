# Optimized Vulkan 1.1 module from 0ba2ee5, compiled with the pinned NDK.
file(SHA256 "${INPUT}" actual)
set(expected "a444aaa8f6ba6e7f39ded988fcd539a53ae3aa5cce70400ba02285c77efdb80d")
if(NOT actual STREQUAL expected)
  message(FATAL_ERROR "Default visibility SPIR-V changed: ${actual}")
endif()
