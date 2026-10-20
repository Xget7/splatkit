#include <gtest/gtest.h>

#include "rendering/metal/device/MetalContext.h"

namespace splatkit {
namespace {

TEST(MetalContextTest, CreateBuildsDeviceQueueLibraryAndDescription) {
  auto context = MetalContext::create();
  ASSERT_NE(context, nullptr);
  EXPECT_NE(context->device(), nil);
  EXPECT_NE(context->queue(), nil);
  ASSERT_NE(context->library(), nil);
  EXPECT_NE([context->library() newFunctionWithName:@"splatFragment"], nil);
  EXPECT_EQ(context->description(), std::string(context->device().name.UTF8String) + ", Metal");
}

#if !TARGET_OS_IPHONE
TEST(MetalContextTest, MacReportsNoMemoryLimit) {
  auto context = MetalContext::create();
  ASSERT_NE(context, nullptr);
  EXPECT_FALSE(context->availableMemory().has_value());
}
#endif

}  // namespace
}  // namespace splatkit
