#include "PlatformPaths.hpp"
#include "PluginTestFixture.hpp"
#include "instrument-script-server/daemon/CommandHandlers.hpp"
#include "instrument-script-server/daemon/DataBufferManager.hpp"
#include "instrument-script-server/daemon/InstrumentRegistry.hpp"
#include "instrument-script-server/daemon/PluginRegistry.hpp"
#include "instrument-script-server/daemon/RuntimeContext.hpp"
#include "instrument-script-server/daemon/ServerDaemon.hpp"
#include "instrument-script-server/daemon/SyncCoordinator.hpp"
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <instrument-call-stack/instrument-call-stack-lua.h>
#include <instrument-call-stack/instrument-call-stack.h>
#include <instrument-data.h>
#include <instrument-log/inst_logging.h>
#include <instrument-plugin.h>
#include <nlohmann/json.hpp>
#include <sol/sol.hpp>

namespace v1 = instserver::daemon::v1;
using namespace instserver;
using namespace instserver::daemon;
using namespace instserver::test;

#ifndef TEST_DATA_DIR
#define TEST_DATA_DIR "."
#endif

class MultiChannelScriptTest : public test::PluginTestFixture {
protected:
  void SetUp() override {
    PluginTestFixture::SetUp();
    inst_log_shutdown();
    inst_log_init("multi_channel_script_test.log", INST_LOG_DEBUG, "instrument",
                  1024 * 1024, // 1 MB
                  3);          // rotation count

    test_scripts_dir_ = std::filesystem::path(TEST_DATA_DIR) / "test_scripts";
    test_configs_dir_ = std::filesystem::path(TEST_DATA_DIR);

    // Create test scripts directory if needed
    std::filesystem::create_directories(test_scripts_dir_);

    // Start daemon
    auto &daemon = ServerDaemon::instance();
    if (!daemon.is_running()) {
      ASSERT_TRUE(daemon.start());
    }

    // Start mock multi-channel instruments
    auto &registry = InstrumentRegistry::instance();

    std::string config1 =
        (test_configs_dir_ / "mock_instrument_multi1.yaml").string();
    std::string config2 =
        (test_configs_dir_ / "mock_instrument_multi2.yaml").string();
    std::string config3 =
        (test_configs_dir_ / "mock_instrument_multi3.yaml").string();

    if (std::filesystem::exists(config1)) {
      registry.create_instrument(config1);
    }
    if (std::filesystem::exists(config2)) {
      registry.create_instrument(config2);
    }
    if (std::filesystem::exists(config3)) {
      registry.create_instrument(config3);
    }
  }

  void TearDown() override {
    auto &registry = InstrumentRegistry::instance();
    registry.stop_all();
    auto &plugin_registry = instserver::plugin::PluginRegistry::instance();
    plugin_registry.unload_all();
    auto &daemon = ServerDaemon::instance();
    if (daemon.is_running()) {
      daemon.stop();
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    inst_log_flush();
    inst_log_shutdown();
  }

  bool run_script(const std::string &script_name) {
    auto script_path = test_scripts_dir_ / script_name;

    if (!std::filesystem::exists(script_path)) {
      LOG_ERROR("TEST", "SCRIPT", "Script not found: %s",
                script_path.string().c_str());
      return false;
    }

    try {
      auto &registry = InstrumentRegistry::instance();
      SyncCoordinator sync_coordinator;

      sol::state lua;
      lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::table,
                         sol::lib::string, sol::lib::io, sol::lib::os);

      bind_runtime_context(lua, registry, sync_coordinator);
      register_instrument_call_stack(lua.lua_state());

      RuntimeContext ctx(registry, ServerDaemon::instance().sync_coordinator());
      lua["context"] = &ctx;

      auto result = lua.safe_script_file(script_path.string());
      if (!result.valid()) {
        sol::error err = result;
        LOG_ERROR("TEST", "SCRIPT", "Script load error: %s", err.what());
        return false;
      }

      sol::function main = lua["main"];
      if (!main.valid()) {
        LOG_ERROR("TEST", "SCRIPT", "No main() function defined in script");
        return false;
      }

      auto call_result = main(lua["context"]);
      if (!call_result.valid()) {
        sol::error err = call_result;
        LOG_ERROR("TEST", "SCRIPT", "Error running main(): %s", err.what());
        return false;
      }

      return true;

    } catch (const std::exception &e) {
      LOG_ERROR("TEST", "SCRIPT", "Exception: %s", e.what());
      return false;
    }
  }

  RuntimeContext *run_script_with_context(const std::string &script_name) {
    auto script_path = test_scripts_dir_ / script_name;

    if (!std::filesystem::exists(script_path)) {
      LOG_ERROR("TEST", "SCRIPT", "Script not found: %s",
                script_path.string().c_str());
      return nullptr;
    }

    try {
      auto &registry = InstrumentRegistry::instance();
      auto &sync_coordinator = sync_coordinator_;

      sol::state lua;
      lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::table,
                         sol::lib::string, sol::lib::io, sol::lib::os);

      bind_runtime_context(lua, registry, sync_coordinator);
      register_instrument_call_stack(lua.lua_state());

      test_context_ = std::make_unique<RuntimeContext>(
          registry, ServerDaemon::instance().sync_coordinator());
      lua["context"] = test_context_.get();

      auto result = lua.safe_script_file(script_path.string());
      if (!result.valid()) {
        sol::error err = result;
        LOG_ERROR("TEST", "SCRIPT", "Script load error: %s", err.what());
        return nullptr;
      }

      sol::function main = lua["main"];
      if (!main.valid()) {
        LOG_ERROR("TEST", "SCRIPT", "No main() function defined in script");
        return nullptr;
      }

      auto call_result = main(lua["context"]);
      if (!call_result.valid()) {
        sol::error err = call_result;
        LOG_ERROR("TEST", "SCRIPT", "Error running main(): %s", err.what());
        return nullptr;
      }

      return test_context_.get();

    } catch (const std::exception &e) {
      LOG_ERROR("TEST", "SCRIPT", "Exception: %s", e.what());
      return nullptr;
    }
  }

  std::filesystem::path test_scripts_dir_;
  std::filesystem::path test_configs_dir_;
  SyncCoordinator sync_coordinator_;
  std::unique_ptr<RuntimeContext> test_context_;
};

TEST_F(MultiChannelScriptTest, ChannelAddressing) {
  EXPECT_TRUE(run_script("channel_addressing_multi.lua"));
}

TEST_F(MultiChannelScriptTest, ChannelAddressingWithReturns) {
  auto *ctx = run_script_with_context("channel_addressing_multi.lua");
  ASSERT_NE(ctx, nullptr);

  const auto &results = ctx->get_results();

  // Should have 4 results: 2 SETs and 2 GETs
  EXPECT_EQ(results.size(), 4);

  // Verify channel addressing in instrument names
  bool has_channel1 = false;
  bool has_channel2 = false;

  for (const auto &result : results) {
    if (instrument_call_stack_get_channel(result.target.get()) == 1) {
      has_channel1 = true;
    }
    if (instrument_call_stack_get_channel(result.target.get()) == 2) {
      has_channel2 = true;
    }
  }

  EXPECT_TRUE(has_channel1);
  EXPECT_TRUE(has_channel2);
}

TEST_F(MultiChannelScriptTest, MultipleReturns) {
  auto *ctx = run_script_with_context("multiple_returns_multi.lua");
  ASSERT_NE(ctx, nullptr);

  const auto &results = ctx->get_results();

  // Should have collected multiple results (8 calls in the script)
  EXPECT_GT(results.size(), 0);
  EXPECT_EQ(results.size(),
            8); // 4 GET calls + 2 SET calls + 2 GET calls with channels

  // Verify all results have basic metadata
  for (const auto &result : results) {
    EXPECT_NE(instrument_call_stack_get_instrument_name(result.target.get()),
              nullptr);
    EXPECT_NE(instrument_call_stack_get_command(result.target.get()), nullptr);
    EXPECT_FALSE(result.returns.empty());
  }

  // Verify we captured returns in order - first should be GET_DOUBLE
  EXPECT_STREQ(instrument_call_stack_get_command(results[0].target.get()),
               "GET_DOUBLE");
  EXPECT_EQ(results[3].returns[0].type, PARAM_TYPE_BUFFER);
  const auto &id = results[3].returns[0].value.str_val;
  data_manager_release_buffer(id);
}

