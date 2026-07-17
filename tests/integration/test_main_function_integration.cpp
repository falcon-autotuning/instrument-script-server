#include "PluginTestFixture.hpp"
#include "instrument-script-server/daemon/CommandHandlers.hpp"
#include "instrument-script-server/daemon/InstrumentRegistry.hpp"
#include "instrument-script-server/daemon/PluginRegistry.hpp"
#include "instrument-script-server/daemon/ServerDaemon.hpp"
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <instrument-log/inst_logging.h>
#include <nlohmann/json.hpp>
#include <sol/sol.hpp>

#include <google/protobuf/util/json_util.h>

#ifndef TEST_DATA_DIR
#define TEST_DATA_DIR "."
#endif

using namespace instserver;
using namespace instserver::test;
using namespace instserver::daemon;
using json = nlohmann::json;

class MainFunctionIntegrationTest : public test::PluginTestFixture {
protected:
  void SetUp() override {
    PluginTestFixture::SetUp();
    auto &daemon = ServerDaemon::instance();
    if (!daemon.is_running()) {
      ASSERT_TRUE(daemon.start());
    }

    // Create temp directory for test scripts
    test_scripts_dir_ =
        std::filesystem::temp_directory_path() / "test_main_scripts";
    std::filesystem::create_directories(test_scripts_dir_);

    // Initialize logger
    log_path_ =
        std::filesystem::temp_directory_path() / "test_main_integration.log";
    inst_log_shutdown();
    inst_log_init(log_path_.string().c_str(), INST_LOG_DEBUG, "instrument",
                  1024 * 1024 * 10, // 10 MB
                  3);               // rotation count

    // Start test instruments
    auto &registry = InstrumentRegistry::instance();

    auto config_dir = std::filesystem::path(TEST_DATA_DIR);
    std::string config1 = (config_dir / "mock_instrument1.yaml").string();

    if (std::filesystem::exists(config1)) {
      registry.create_instrument(config1);
    }
  }

  void TearDown() override {
    auto &daemon = ServerDaemon::instance();
    if (daemon.is_running()) {
      ASSERT_NO_THROW(daemon.stop());
    }
    auto &registry = InstrumentRegistry::instance();
    registry.stop_all();
    auto &plugin_registry = instserver::plugin::PluginRegistry::instance();
    plugin_registry.unload_all();

    // Clean up
    inst_log_flush();
    inst_log_shutdown();
    std::error_code ec;
    std::filesystem::remove_all(test_scripts_dir_, ec);
    std::filesystem::remove(log_path_, ec);
  }

  std::string read_log() {
    inst_log_flush();
    // Increased wait time for slower systems
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    std::ifstream ifs(log_path_, std::ios::in | std::ios::binary);
    if (!ifs) {
      return "";
    }
    return {(std::istreambuf_iterator<char>(ifs)),
            (std::istreambuf_iterator<char>())};
  }

  void create_test_script(const std::string &name, const std::string &content) {
    auto script_path = test_scripts_dir_ / name;
    std::ofstream ofs(script_path);
    ofs << content;
    ofs.close();
  }

  std::filesystem::path test_scripts_dir_;
  std::filesystem::path log_path_;
};

TEST_F(MainFunctionIntegrationTest, MainFunction) {
  create_test_script("new_format.lua", R"lua(
    function main(ctx)
      ctx:log("New format script")
      return nil
    end
  )lua");

  MeasureJobRequest req{};
  req.set_script_path((test_scripts_dir_ / "new_format.lua").string());
  MeasureJobResultResponse resp{};
  int result = handle_measure(req, &resp);

  EXPECT_EQ(result, 0);
  EXPECT_TRUE(resp.standard_response().ok());

  auto log = read_log();
  EXPECT_NE(log.find("New format script"), std::string::npos);
}

TEST_F(MainFunctionIntegrationTest, ReturnedModuleMainFunction) {
  create_test_script("returned_module.lua", R"lua(
    local function ModuleMain(ctx)
      ctx:log("Returned module script")
      return nil
    end

    return { main = ModuleMain }
  )lua");

  MeasureJobRequest req{};
  req.set_script_path((test_scripts_dir_ / "returned_module.lua").string());
  MeasureJobResultResponse resp{};
  int result = handle_measure(req, &resp);

  EXPECT_EQ(result, 0);
  EXPECT_TRUE(resp.standard_response().ok());

  auto log = read_log();
  EXPECT_NE(log.find("Returned module script"), std::string::npos);
}

// Test global variable injection with warnings
TEST_F(MainFunctionIntegrationTest, GlobalVariableInjectionWithWarnings) {
  create_test_script("with_globals.lua", R"lua(
    function main(ctx)
      ctx:log("Test voltage: " .. tostring(testVoltage))
      ctx:log("Sample rate: " .. tostring(sampleRate))
      return nil
    end
  )lua");

  MeasureJobRequest req{};
  req.set_script_path((test_scripts_dir_ / "with_globals.lua").string());

  auto *globals = req.mutable_globals();
  auto *map = globals->mutable_map();

  VariableValue voltage_val;
  voltage_val.set_d(5.0);
  (*map)["testVoltage"] = voltage_val;

  VariableValue sample_rate_val;
  sample_rate_val.set_i(1000);
  (*map)["sampleRate"] = sample_rate_val;

  MeasureJobResultResponse resp{};
  int result = handle_measure(req, &resp);

  EXPECT_EQ(result, 0);
  EXPECT_TRUE(resp.standard_response().ok());

  auto log = read_log();
  EXPECT_NE(log.find("Global variable 'testVoltage'"), std::string::npos);
  EXPECT_NE(log.find("Global variable 'sampleRate'"), std::string::npos);
  EXPECT_NE(log.find("Test voltage: 5"), std::string::npos);
  EXPECT_NE(log.find("Sample rate: 1000"), std::string::npos);
}

// Test context:error() in new format
TEST_F(MainFunctionIntegrationTest, ContextErrorInNewFormat) {
  create_test_script("with_error.lua", R"lua(
    function main(ctx)
      ctx:log("Before error")
      ctx:error("Test error condition")
      return nil
    end
  )lua");

  MeasureJobRequest req{};
  req.set_script_path((test_scripts_dir_ / "with_error.lua").string());

  MeasureJobResultResponse resp{};
  int result = handle_measure(req, &resp);

  EXPECT_EQ(result, 1);
  EXPECT_FALSE(resp.standard_response().ok());
  EXPECT_EQ(resp.standard_response().error().message(), "Test error condition");

  auto log = read_log();
  EXPECT_NE(log.find("Test error condition"), std::string::npos);
  EXPECT_NE(log.find("Before error"), std::string::npos);
}

// Test runtime error capture in new format
TEST_F(MainFunctionIntegrationTest, RuntimeErrorCapture) {
  create_test_script("runtime_error.lua", R"lua(
    function main(ctx)
      ctx:log("Before runtime error")
      error("Lua runtime error")
      return nil
    end
  )lua");

  MeasureJobRequest req{};
  req.set_script_path((test_scripts_dir_ / "runtime_error.lua").string());

  MeasureJobResultResponse resp{};
  int result = handle_measure(req, &resp);

  EXPECT_EQ(result, 1);
  EXPECT_FALSE(resp.standard_response().ok());
  EXPECT_NE(resp.standard_response().error().message().find("runtime error"),
            std::string::npos);
}

// Test combined context:error() and runtime error
TEST_F(MainFunctionIntegrationTest, CombinedErrorMessages) {
  create_test_script("combined_error.lua", R"lua(
    function main(ctx)
      ctx:error("Context error")
      error("Runtime error")
      return nil
    end
  )lua");

  MeasureJobRequest req{};
  req.set_script_path((test_scripts_dir_ / "combined_error.lua").string());

  MeasureJobResultResponse resp{};
  int result = handle_measure(req, &resp);

  EXPECT_EQ(result, 1);
  EXPECT_FALSE(resp.standard_response().ok());

  // Should contain both error messages
  std::string error_msg = resp.standard_response().error().message();
  EXPECT_NE(error_msg.find("Context error"), std::string::npos);
  EXPECT_NE(error_msg.find("Runtime"), std::string::npos);
}

// Test that main receives correct context type
TEST_F(MainFunctionIntegrationTest, MainReceivesCorrectContextType) {
  create_test_script("context_type.lua", R"lua(
    function main(ctx)
      -- Test that ctx has expected methods
      if type(ctx.log) == "function" then
        ctx:log("Context has log method")
      end
      if type(ctx.call) == "function" then
        ctx:log("Context has call method")
      end
      if type(ctx.error) == "function" then
        ctx:log("Context has error method")
      end
      return nil
    end
  )lua");

  MeasureJobRequest req{};
  req.set_script_path((test_scripts_dir_ / "context_type.lua").string());

  MeasureJobResultResponse resp{};
  int result = handle_measure(req, &resp);

  EXPECT_EQ(result, 0);
  EXPECT_TRUE(resp.standard_response().ok());

  auto log = read_log();
  EXPECT_NE(log.find("Context has log method"), std::string::npos);
  EXPECT_NE(log.find("Context has call method"), std::string::npos);
  EXPECT_NE(log.find("Context has error method"), std::string::npos);
}
