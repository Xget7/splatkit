#include <cstdio>

#include "tests/VulkanTestContext.h"

int main(int argc, char** argv) {
  using splatkit::GpuBuffer;
  namespace test = splatkit::test;
  using test::require;
  try {
    const bool small = argc == 2 && std::strcmp(argv[1], "--small") == 0;
    require(argc == 1 || small, "usage: splatkit_gpu_buffer_test [--small]");
    test::VulkanTestContext gpu;
    std::printf("GPU: %s\n", gpu.context->deviceDescription().c_str());
    std::vector<uint8_t> expected(small ? 4096 : 16000000);
    uint32_t pattern = 1;
    for (auto& byte : expected) {
      pattern = pattern * 1664525U + 1013904223U;
      byte = pattern >> 24;
    }
    auto buffer = GpuBuffer::deviceLocal(
        *gpu.context, expected.size(),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    require(buffer != nullptr, "allocate device buffer");
    auto order0 = GpuBuffer::hostVisible(*gpu.context, 2000000, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    require(order0 && order0->mapped(), "allocate live mapped buffer before staging");
    std::printf("upload %zu bytes\n", expected.size());
    std::fflush(stdout);
    require(buffer->upload(expected.data(), expected.size()), "upload");
    require(gpu.readback(*buffer, expected.size()) == expected, "GPU round trip");
    std::puts("PASS GPU round trip");
    if (small) std::puts("SKIP 16 MB mapping regression (--small requested)");
    const std::vector<uint8_t> patch{9, 4, 7, 1, 8};
    require(buffer->upload(13, patch.data(), patch.size()), "unaligned byte range upload");
    std::copy(patch.begin(), patch.end(), expected.begin() + 13);
    require(gpu.readback(*buffer, expected.size()) == expected,
            "partial upload preserves neighbors");
    require(!buffer->upload(nullptr, 1), "reject nonempty null upload");
    require(!buffer->upload(buffer->size() - 1, patch.data(), patch.size()), "reject overrun");
    require(!buffer->upload(UINT64_MAX, patch.data(), 4), "reject overflowing offset");
    require(!buffer->upload(1, patch.data(), UINT64_MAX), "reject overflowing size");
    require(buffer->upload(buffer->size(), nullptr, 0), "empty upload at end");
    require(!buffer->upload(buffer->size() + 1, nullptr, 0), "reject empty upload past end");
    require(!GpuBuffer::deviceLocal(*gpu.context, 0, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT),
            "reject empty device allocation");
    require(!GpuBuffer::hostVisible(*gpu.context, 0, VK_BUFFER_USAGE_TRANSFER_SRC_BIT),
            "reject empty mapped allocation");
    std::memset(order0->mapped(), 0x62, static_cast<size_t>(order0->size()));
    order0->flush(0, order0->size());
    require(gpu.readback(*order0, order0->size()) == std::vector<uint8_t>(order0->size(), 0x62),
            "mapped host writes reach GPU");
    gpu.requireValidationClean();
    std::printf("PASS buffer regressions; validation=%s\n",
                gpu.context->validationEnabled() ? "on" : "off");
    return 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    return 1;
  }
}
