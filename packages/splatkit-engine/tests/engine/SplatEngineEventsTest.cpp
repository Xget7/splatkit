#include <gtest/gtest.h>

#include <atomic>
#include <future>
#include <memory>

#include "splatkit/engine/SplatEngine.h"
#include "support/StubRenderer.h"

namespace splatkit {
namespace {

TEST(SplatEngineEvents, RemovingSinkKeepsItsCapturesAliveUntilCallbackReturns) {
  SplatEngine engine(std::make_unique<StubRenderer>());
  auto owner = std::make_shared<int>(1);
  const std::weak_ptr<int> lifetime = owner;
  bool called = false;
  engine.setEventSink([&, owner](SplatEngine::Event, const std::string&, uint32_t) {
    engine.setEventSink({});
    EXPECT_FALSE(lifetime.expired());
    called = true;
  });
  owner.reset();
  engine.loadWorld(nullptr, 0);
  EXPECT_TRUE(called);
  EXPECT_TRUE(lifetime.expired());
}

TEST(SplatEngineEvents, ClearingDoesNotWaitForAnActiveCallbackOrDestroyItsCaptures) {
  SplatEngine engine(std::make_unique<StubRenderer>());
  auto owner = std::make_shared<int>(1);
  const std::weak_ptr<int> lifetime = owner;
  std::promise<void> entered;
  std::promise<void> release;
  auto released = release.get_future();
  engine.setEventSink([&, owner](SplatEngine::Event, const std::string&, uint32_t) {
    entered.set_value();
    released.wait();
  });
  owner.reset();
  auto load = std::async(std::launch::async, [&] { engine.loadWorld(nullptr, 0); });
  entered.get_future().wait();
  auto clear = std::async(std::launch::async, [&] { engine.setEventSink({}); });
  const auto status = clear.wait_for(std::chrono::seconds(2));
  EXPECT_EQ(status, std::future_status::ready);
  if (status == std::future_status::ready) EXPECT_FALSE(lifetime.expired());
  release.set_value();
  load.get();
  clear.get();
  EXPECT_TRUE(lifetime.expired());
}

TEST(SplatEngineEvents, CallbackMayReplaceSinkAndReportAnotherFailure) {
  SplatEngine engine(std::make_unique<StubRenderer>());
  int replacements = 0;
  engine.setEventSink([&](SplatEngine::Event, const std::string&, uint32_t) {
    engine.setEventSink([&](SplatEngine::Event, const std::string&, uint32_t) { ++replacements; });
    engine.loadWorld(nullptr, 0);
  });
  engine.loadWorld(nullptr, 0);
  EXPECT_EQ(replacements, 1);
}

TEST(SplatEngineEvents, CaptureDestructionCanReenterSinkReplacement) {
  SplatEngine engine(std::make_unique<StubRenderer>());
  // A control block with a release hook models captures that reenter their host.
  auto owner = std::shared_ptr<void>(nullptr, [&engine](void*) { engine.setEventSink({}); });
  engine.setEventSink([owner](SplatEngine::Event, const std::string&, uint32_t) {});
  owner.reset();
  engine.setEventSink({});
}

TEST(SplatEngineEvents, ConcurrentReplacementAndFailures) {
  SplatEngine engine(std::make_unique<StubRenderer>());
  std::atomic<int> calls{0};
  auto load = std::async(std::launch::async, [&] {
    for (int i = 0; i < 200; ++i) engine.loadWorld(nullptr, 0);
  });
  for (int i = 0; i < 200; ++i) {
    engine.setEventSink([&](SplatEngine::Event, const std::string&, uint32_t) { ++calls; });
    engine.setEventSink({});
  }
  load.get();
  engine.setEventSink([&](SplatEngine::Event, const std::string&, uint32_t) { ++calls; });
  engine.loadWorld(nullptr, 0);
  EXPECT_GT(calls.load(), 0);
}

}  // namespace
}  // namespace splatkit
