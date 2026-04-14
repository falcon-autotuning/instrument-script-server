#include "PlatformPaths.hpp"
#include "PluginTestFixture.hpp"
#include "instrument-script-server/Logger.hpp"
#include "instrument-script-server/plugin/PluginRegistry.hpp"
#include "instrument-script-server/server/CommandHandlers.hpp"
#include "instrument-script-server/server/InstrumentRegistry.hpp"
#include "instrument-script-server/server/RuntimeContext.hpp"
#include "instrument-script-server/server/ServerDaemon.hpp"
#include "instrument-script-server/server/SyncCoordinator.hpp"
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <sol/sol.hpp>

using namespace instserver;
using namespace instserver::test;
using namespace instserver::server;
using json = nlohmann::json;

class MainFunctionIntegrationTest : public test::PluginTestFixture {
protected:
  void SetUp() override {
    PluginTestFixture::SetUp();

    // Create temp directory for test scripts
    test_scripts_dir_ =
        std::filesystem::temp_directory_path() / "test_main_scripts";
    std::filesystem::create_directories(test_scripts_dir_);

    // Initialize logger
    log_path_ =
        std::filesystem::temp_directory_path() / "test_main_integration.log";
    InstrumentLogger::instance().shutdown();
    InstrumentLogger::instance().init(log_path_.string(), spdlog::level::debug);

    if (auto l = spdlog::get("instrument")) {
      l->flush_on(spdlog::level::debug);
    }

    // Start test instruments
    auto &registry = InstrumentRegistry::instance();

    auto config_dir = std::filesystem::current_path() / "data";
    std::string config1 = (config_dir / "mock_instrument1.yaml").string();

    if (std::filesystem::exists(config1)) {
      registry.create_instrument(config1);
    }
  }

  void TearDown() override {
    auto &registry = InstrumentRegistry::instance();
    registry.stop_all();

    // Clean up
    InstrumentLogger::instance().shutdown();
    std::error_code ec;
    std::filesystem::remove_all(test_scripts_dir_, ec);
    std::filesystem::remove(log_path_, ec);
  }

  std::string read_log() {
    if (auto l = spdlog::get("instrument")) {
      l->flush();
    }
    // Increased wait time for slower systems
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    std::ifstream ifs(log_path_, std::ios::in | std::ios::binary);
    if (!ifs)
      return "";
    return std::string((std::istreambuf_iterator<char>(ifs)),
                       (std::istreambuf_iterator<char>()));
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

// Test new format script with main function
TEST_F(MainFunctionIntegrationTest, NewFormatWithMainFunction) {
  create_test_script("new_format.lua", R"lua(
    function main(ctx)
      ctx:log("New format script")
      return nil
    end
  )lua");

  json params;
  params["script_path"] = (test_scripts_dir_ / "new_format.lua").string();

  json out;
  int result = handle_measure(params, out);

  EXPECT_EQ(result, 0);
  EXPECT_TRUE(out["ok"].get<bool>());

  auto log = read_log();
  EXPECT_NE(log.find("New format script"), std::string::npos);
  EXPECT_NE(log.find("main function (new format)"), std::string::npos);
}

// Test compatibility mode with deprecation warning
TEST_F(MainFunctionIntegrationTest, CompatibilityModeDeprecationWarning) {
  create_test_script("old_format.lua", R"lua(
    context:log("Old format script")
  )lua");

  json params;
  params["script_path"] = (test_scripts_dir_ / "old_format.lua").string();

  json out;
  int result = handle_measure(params, out);

  EXPECT_EQ(result, 0);
  EXPECT_TRUE(out.contains("error"));
  EXPECT_NE(out["error"].get<std::string>().find("DEPRECATED"),
            std::string::npos);

  auto log = read_log();
  EXPECT_NE(log.find("DEPRECATED"), std::string::npos);
  EXPECT_NE(log.find("compatibility mode"), std::string::npos);
  EXPECT_NE(log.find("Old format script"), std::string::npos);
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

  json params;
  params["script_path"] = (test_scripts_dir_ / "with_globals.lua").string();
  params["globals"] = {{"testVoltage", 5.0}, {"sampleRate", 1000}};

  json out;
  int result = handle_measure(params, out);

  EXPECT_EQ(result, 0);
  EXPECT_TRUE(out["ok"].get<bool>());

  auto log = read_log();
  EXPECT_NE(log.find("Injecting global variable 'testVoltage'"),
            std::string::npos);
  EXPECT_NE(log.find("Injecting global variable 'sampleRate'"),
            std::string::npos);
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

  json params;
  params["script_path"] = (test_scripts_dir_ / "with_error.lua").string();

  json out;
  int result = handle_measure(params, out);

  EXPECT_EQ(result, 1);
  EXPECT_FALSE(out["ok"].get<bool>());
  EXPECT_EQ(out["error"].get<std::string>(), "Test error condition");

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

  json params;
  params["script_path"] = (test_scripts_dir_ / "runtime_error.lua").string();

  json out;
  int result = handle_measure(params, out);

  EXPECT_EQ(result, 1);
  EXPECT_FALSE(out["ok"].get<bool>());
  EXPECT_NE(out["error"].get<std::string>().find("runtime error"),
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

  json params;
  params["script_path"] = (test_scripts_dir_ / "combined_error.lua").string();

  json out;
  int result = handle_measure(params, out);

  EXPECT_EQ(result, 1);
  EXPECT_FALSE(out["ok"].get<bool>());

  // Should contain both error messages
  std::string error_msg = out["error"].get<std::string>();
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

  json params;
  params["script_path"] = (test_scripts_dir_ / "context_type.lua").string();

  json out;
  int result = handle_measure(params, out);

  EXPECT_EQ(result, 0);
  EXPECT_TRUE(out["ok"].get<bool>());

  auto log = read_log();
  EXPECT_NE(log.find("Context has log method"), std::string::npos);
  EXPECT_NE(log.find("Context has call method"), std::string::npos);
  EXPECT_NE(log.find("Context has error method"), std::string::npos);
}
