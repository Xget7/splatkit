#include "engine/AndroidEngine.h"

#include <android/log.h>

#include "splatkit/Log.h"

namespace splatkit {
namespace {

void logcatSink(LogLevel level, const char* message) {
  const int priority = level == LogLevel::error  ? ANDROID_LOG_ERROR
                       : level == LogLevel::warn ? ANDROID_LOG_WARN
                                                 : ANDROID_LOG_INFO;
  __android_log_write(priority, "SplatKit", message);
}

}  // namespace

splat::Result<std::unique_ptr<AndroidEngine>> AndroidEngine::create() {
  setLogSink(&logcatSink);
  std::unique_ptr<AndroidEngine> host(new AndroidEngine());
  auto ctx = VulkanContext::create();
  if (!ctx) return ctx.error();
  host->ctx_ = std::move(ctx.value());
  host->frameLoop_ = std::make_unique<FrameLoop>(*host->ctx_);
  if (!host->frameLoop_->valid()) {
    return splat::Error{splat::ErrorCode::gpuUnavailable, "frame loop"};
  }
  auto renderer = std::make_unique<VulkanSplatRenderer>(*host->ctx_, *host->frameLoop_);
  host->renderer_ = renderer.get();
  host->engine_ = std::make_unique<SplatEngine>(std::move(renderer));
  return host;
}

AndroidEngine::~AndroidEngine() {
  engine_.reset();
  frameLoop_.reset();
  if (ctx_) ctx_->waitIdle();
}

}  // namespace splatkit
