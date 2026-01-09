#include "PluginTestFixture.hpp"
#include "instrument-server/Logger.hpp"
#include "instrument-server/server/InstrumentRegistry.hpp"
#include "instrument-server/server/RuntimeContext.hpp"
#include "instrument-server/server/ServerDaemon.hpp"
#include "instrument-server/server/SyncCoordinator.hpp"
#include <filesystem>
#include <gtest/gtest.h>
#include <sol/sol.hpp>

using namespace instserver;

class MeasurementScriptTest : public test::PluginTestFixture {
protected:
  void SetUp() override {
    PluginTestFixture::SetUp();
    InstrumentLogger::instance().init("script_test.log", spdlog::level::debug);

    test_scripts_dir_ =
        std::filesystem::current_path() / "tests" / "data" / "test_scripts";
    test_configs_dir_ = std::filesystem::current_path() / "tests" / "data";

    // Create test scripts directory if needed
    std::filesystem::create_directories(test_scripts_dir_);

    // Start daemon
    auto &daemon = ServerDaemon::instance();
    if (!daemon.is_running()) {
      ASSERT_TRUE(daemon.start());
    }

    // Start mock instruments
    auto &registry = InstrumentRegistry::instance();

    std::string config1 = test_configs_dir_ / "mock_instrument1.yaml";
    std::string config2 = test_configs_dir_ / "mock_instrument2.yaml";
    std::string config3 = test_configs_dir_ / "mock_instrument3.yaml";

    if (std::filesystem::exists(config1))
      registry.create_instrument(config1);
    if (std::filesystem::exists(config2))
      registry.create_instrument(config2);
    if (std::filesystem::exists(config3))
      registry.create_instrument(config3);
  }

  void TearDown() override {
    auto &registry = InstrumentRegistry::instance();
    registry.stop_all();
  }

  bool run_script(const std::string &script_name) {
    auto script_path = test_scripts_dir_ / script_name;

    if (!std::filesystem::exists(script_path)) {
      LOG_ERROR("TEST", "SCRIPT", "Script not found: {}", script_path.string());
      return false;
    }

    try {
      auto &registry = InstrumentRegistry::instance();
      SyncCoordinator sync_coordinator;

      sol::state lua;
      lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::table,
                         sol::lib::string, sol::lib::io, sol::lib::os);

      bind_runtime_context(lua, registry, sync_coordinator);

      RuntimeContext ctx(registry, sync_coordinator);
      lua["context"] = &ctx;

      auto result = lua.safe_script_file(script_path.string());

      if (!result.valid()) {
        sol::error err = result;
        LOG_ERROR("TEST", "SCRIPT", "Script error: {}", err.what());
        return false;
      }

      return true;

    } catch (const std::exception &e) {
      LOG_ERROR("TEST", "SCRIPT", "Exception: {}", e.what());
      return false;
    }
  }

  RuntimeContext* run_script_with_context(const std::string &script_name) {
    auto script_path = test_scripts_dir_ / script_name;

    if (!std::filesystem::exists(script_path)) {
      LOG_ERROR("TEST", "SCRIPT", "Script not found: {}", script_path.string());
      return nullptr;
    }

    try {
      auto &registry = InstrumentRegistry::instance();
      auto &sync_coordinator = sync_coordinator_;

      sol::state lua;
      lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::table,
                         sol::lib::string, sol::lib::io, sol::lib::os);

      bind_runtime_context(lua, registry, sync_coordinator);

      // Use member context
      test_context_ = std::make_unique<RuntimeContext>(registry, sync_coordinator);
      lua["context"] = test_context_.get();

      auto result = lua.safe_script_file(script_path.string());

      if (!result.valid()) {
        sol::error err = result;
        LOG_ERROR("TEST", "SCRIPT", "Script error: {}", err.what());
        return nullptr;
      }

      return test_context_.get();

    } catch (const std::exception &e) {
      LOG_ERROR("TEST", "SCRIPT", "Exception: {}", e.what());
      return nullptr;
    }
  }

  std::filesystem::path test_scripts_dir_;
  std::filesystem::path test_configs_dir_;
  SyncCoordinator sync_coordinator_;
  std::unique_ptr<RuntimeContext> test_context_;
};

TEST_F(MeasurementScriptTest, SimpleCall) {
  EXPECT_TRUE(run_script("simple_call.lua"));
}

TEST_F(MeasurementScriptTest, ParallelExecution) {
  EXPECT_TRUE(run_script("parallel_test.lua"));
}

TEST_F(MeasurementScriptTest, LoopMeasurement) {
  EXPECT_TRUE(run_script("loop_measurement.lua"));
}

TEST_F(MeasurementScriptTest, NestedParallel) {
  EXPECT_TRUE(run_script("nested_parallel.lua"));
}

TEST_F(MeasurementScriptTest, ErrorHandling) {
  // This script intentionally calls non-existent instrument
  // Should complete without crashing
  EXPECT_TRUE(run_script("error_handling.lua"));
}

TEST_F(MeasurementScriptTest, ChannelAddressing) {
  EXPECT_TRUE(run_script("channel_addressing.lua"));
}

TEST_F(MeasurementScriptTest, ReturnTypes) {
  EXPECT_TRUE(run_script("return_types.lua"));
}

TEST_F(MeasurementScriptTest, TableParameters) {
  EXPECT_TRUE(run_script("table_params.lua"));
}

TEST_F(MeasurementScriptTest, ScriptWithOutput) {
  auto script_path = test_scripts_dir_ / "loop_measurement.lua";

  if (!std::filesystem::exists(script_path)) {
    GTEST_SKIP() << "Script not found";
  }

  // Capture stdout
  testing::internal::CaptureStdout();

  EXPECT_TRUE(run_script("loop_measurement.lua"));

  std::string output = testing::internal::GetCapturedStdout();

  // Verify some output was produced
  EXPECT_FALSE(output.empty());
}

TEST_F(MeasurementScriptTest, MultipleReturns) {
  auto ctx = run_script_with_context("multiple_returns.lua");
  ASSERT_NE(ctx, nullptr);
  
  const auto &results = ctx->get_results();
  
  // Should have collected multiple results (8 calls in the script)
  EXPECT_GT(results.size(), 0);
  EXPECT_EQ(results.size(), 8); // 4 GET calls + 2 SET calls + 2 GET calls with channels
  
  // Verify all results have basic metadata
  for (const auto &result : results) {
    EXPECT_FALSE(result.instrument_name.empty());
    EXPECT_FALSE(result.verb.empty());
    EXPECT_FALSE(result.return_type.empty());
  }
  
  // Verify we captured returns in order - first should be GET_DOUBLE
  EXPECT_TRUE(results[0].verb.find("GET_DOUBLE") != std::string::npos ||
              results[0].verb == "GET_DOUBLE");
}

TEST_F(MeasurementScriptTest, ChannelAddressingWithReturns) {
  auto ctx = run_script_with_context("channel_addressing.lua");
  ASSERT_NE(ctx, nullptr);
  
  const auto &results = ctx->get_results();
  
  // Should have 4 results: 2 SETs and 2 GETs
  EXPECT_EQ(results.size(), 4);
  
  // Verify channel addressing in instrument names
  bool has_channel1 = false;
  bool has_channel2 = false;
  
  for (const auto &result : results) {
    if (result.instrument_name.find(":1") != std::string::npos) {
      has_channel1 = true;
    }
    if (result.instrument_name.find(":2") != std::string::npos) {
      has_channel2 = true;
    }
  }
  
  EXPECT_TRUE(has_channel1);
  EXPECT_TRUE(has_channel2);
}
