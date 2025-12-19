#include "instrument-server/Logger.hpp"
#include "instrument-server/server/InstrumentRegistry.hpp"
#include "instrument-server/server/RuntimeContext.hpp"
#include "instrument-server/server/ServerDaemon.hpp"
#include "instrument-server/server/SyncCoordinator.hpp"
#include <filesystem>
#include <gtest/gtest.h>
#include <sol/sol.hpp>

using namespace instserver;

class MeasurementScriptTest : public ::testing::Test {
protected:
  void SetUp() override {
    InstrumentLogger::instance().init("script_test. log", spdlog::level::debug);

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

  std::filesystem::path test_scripts_dir_;
  std::filesystem::path test_configs_dir_;
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
  EXPECT_TRUE(run_script("error_handling. lua"));
}

TEST_F(MeasurementScriptTest, ChannelAddressing) {
  EXPECT_TRUE(run_script("channel_addressing.lua"));
}

TEST_F(MeasurementScriptTest, ReturnTypes) {
  EXPECT_TRUE(run_script("return_types.lua"));
}

TEST_F(MeasurementScriptTest, TableParameters) {
  EXPECT_TRUE(run_script("table_params. lua"));
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
