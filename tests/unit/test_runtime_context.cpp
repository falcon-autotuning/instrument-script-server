#include "TestFixtures.hpp"
#include "instrument-server/server/RuntimeContext.hpp"
#include <gtest/gtest.h>

using namespace instserver;
using namespace instserver::test;

class RuntimeContextTest : public InstrumentServerTest {
protected:
  void SetUp() override {
    InstrumentServerTest::SetUp();

    // Create Lua state
    lua_ = std::make_unique<sol::state>();
    lua_->open_libraries(sol::lib::base, sol::lib::math);

    // Bind runtime contexts
    bind_runtime_contexts(*lua_, *registry_, *sync_coordinator_);
  }

  std::unique_ptr<sol::state> lua_;
};

TEST_F(RuntimeContextTest, CreateDCContext) {
  lua_->script(R"(
        ctx = RuntimeContext_DCGetSet()
    )");

  auto ctx = (*lua_)["ctx"].get<RuntimeContext_DCGetSet *>();
  ASSERT_NE(ctx, nullptr);
  EXPECT_EQ(ctx->sampleRate, 1000.0);
  EXPECT_EQ(ctx->numPoints, 100);
}

TEST_F(RuntimeContextTest, SetContextFields) {
  lua_->script(R"(
        ctx = RuntimeContext_DCGetSet()
        ctx.sampleRate = 5000.0
        ctx.numPoints = 200
    )");

  auto ctx = (*lua_)["ctx"].get<RuntimeContext_DCGetSet *>();
  EXPECT_EQ(ctx->sampleRate, 5000.0);
  EXPECT_EQ(ctx->numPoints, 200);
}

TEST_F(RuntimeContextTest, ParallelBlockBuffering) {
  // This test verifies that commands are buffered during parallel block
  // We can't easily test full execution without real instruments,
  // but we can verify the API works

  lua_->script(R"(
        ctx = RuntimeContext_DCGetSet()
        
        -- This should not crash
        ctx: parallel(function()
            -- Commands would be buffered here
        end)
    )");

  // If we get here without exception, the API works
  SUCCEED();
}

TEST_F(RuntimeContextTest, LogFunction) {
  lua_->script(R"(
        ctx = RuntimeContext_DCGetSet()
        ctx:log("Test log message")
    )");

  // Check that logging doesn't crash
  SUCCEED();
}

TEST_F(RuntimeContextTest, InstrumentTarget) {
  lua_->script(R"(
        target = InstrumentTarget()
        target. id = "DAC1"
        target.channel = 3
        
        serialized = target:serialize()
    )");

  std::string serialized = (*lua_)["serialized"];
  EXPECT_EQ(serialized, "DAC1:3");
}

TEST_F(RuntimeContextTest, Domain) {
  lua_->script(R"(
        domain = Domain()
        domain.min = -10.0
        domain.max = 10.0
    )");

  auto domain = (*lua_)["domain"].get<Domain>();
  EXPECT_EQ(domain.min, -10.0);
  EXPECT_EQ(domain.max, 10.0);
}

TEST_F(RuntimeContextTest, SetVoltageDomains) {
  lua_->script(R"(
        ctx = RuntimeContext_1DWaveform()
        ctx.setVoltageDomains = {
            ["DAC1:1"] = {min = -5.0, max = 5.0},
            ["DAC1:2"] = {min = 0.0, max = 10.0}
        }
    )");

  auto ctx = (*lua_)["ctx"].get<RuntimeContext_1DWaveform *>();
  ASSERT_EQ(ctx->setVoltageDomains.size(), 2);
  EXPECT_EQ(ctx->setVoltageDomains["DAC1:1"].min, -5.0);
  EXPECT_EQ(ctx->setVoltageDomains["DAC1:1"].max, 5.0);
}
