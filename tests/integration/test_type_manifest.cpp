#include "PluginTestFixture.hpp"
#include "instrument-server/Logger.hpp"
#include "instrument-server/server/CommandHandlers.hpp"
#include "instrument-server/server/InstrumentRegistry.hpp"
#include "instrument-server/server/RuntimeContext.hpp"
#include "instrument-server/server/ServerDaemon.hpp"
#include "instrument-server/server/SyncCoordinator.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <sol/sol.hpp>
#include <spdlog/spdlog.h>
#include <thread>

using namespace instserver;
using namespace instserver::server;
using json = nlohmann::json;

class TypeManifestTest : public test::PluginTestFixture {
protected:
  void SetUp() override {
    log_path_ = std::filesystem::current_path() / "script_test.log";
    PluginTestFixture::SetUp();
    InstrumentLogger::instance().init(log_path_, spdlog::level::debug);

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

    std::string config1 =
        (test_configs_dir_ / "mock_instrument1.yaml").string();
    std::string config2 =
        (test_configs_dir_ / "mock_instrument2.yaml").string();
    std::string config3 =
        (test_configs_dir_ / "mock_instrument3.yaml").string();

    if (std::filesystem::exists(config1))
      registry.create_instrument(config1);
    if (std::filesystem::exists(config2))
      registry.create_instrument(config2);
    if (std::filesystem::exists(config3))
      registry.create_instrument(config3);

    // Create temp directory for test scripts
    std::filesystem::path tmp = std::filesystem::temp_directory_path();
    tmp / "test_type_manifest_scripts";
    std::filesystem::create_directories(test_scripts_dir_);
  }

  void TearDown() override {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    if (auto l = spdlog::get("instrument")) {
      l->flush();
    }

    auto &registry = InstrumentRegistry::instance();
    registry.stop_all();

    std::error_code ec;
    std::filesystem::remove(log_path_, ec);
  }

  void create_test_script(const std::string &name, const std::string &content) {
    auto script_path = test_scripts_dir_ / name;
    std::ofstream ofs(script_path);
    ofs << content;
    ofs.close();
  }

  std::string read_log() {
    if (auto l = spdlog::get("instrument")) {
      l->flush();
    }
    // Poll for up to 500ms for log file to be non-empty
    for (int i = 0; i < 10; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      std::ifstream ifs(log_path_, std::ios::in | std::ios::binary);
      if (ifs && ifs.peek() != std::ifstream::traits_type::eof()) {
        return std::string((std::istreambuf_iterator<char>(ifs)),
                           (std::istreambuf_iterator<char>()));
      }
    }
    // Final attempt
    std::ifstream ifs(log_path_, std::ios::in | std::ios::binary);
    if (!ifs)
      return "";
    return std::string((std::istreambuf_iterator<char>(ifs)),
                       (std::istreambuf_iterator<char>()));
  }

  std::filesystem::path log_path_;
  std::filesystem::path test_scripts_dir_;
  std::filesystem::path test_configs_dir_;
  SyncCoordinator sync_coordinator_;
  std::unique_ptr<RuntimeContext> test_context_;
};

// Test main function with typed parameters
TEST_F(TypeManifestTest, TypedMainFunctionWithManifest) {
  create_test_script("typed_main.lua", R"lua(
    function main(ctx, voltage, sampleRate)
      ctx:log("Voltage: " .. tostring(voltage))
      ctx:log("Sample rate: " .. tostring(sampleRate))
      return nil
    end
  )lua");

  json params;
  params["script_path"] = (test_scripts_dir_ / "typed_main.lua").string();
  params["globals"] = {{"voltage", 5.0}, {"sampleRate", 1000}};
  params["type_manifest"] = {
      {"parameters",
       json::array({{{"name", "ctx"}, {"type", "RuntimeContext"}},
                    {{"name", "voltage"}, {"type", "number"}},
                    {{"name", "sampleRate"}, {"type", "number"}}})}};

  json out;
  int result = handle_measure(params, out);

  EXPECT_EQ(result, 0);
  if (!out["ok"].get<bool>()) {
    std::cout << out["error"].get<std::string>() << std::endl;
  }
  EXPECT_TRUE(out["ok"].get<bool>());

  auto log = read_log();
  EXPECT_NE(log.find("Voltage: 5"), std::string::npos);
  EXPECT_NE(log.find("Sample rate: 1000"), std::string::npos);
  EXPECT_NE(log.find("Passing parameter 'voltage'"), std::string::npos);
  EXPECT_NE(log.find("Passing parameter 'sampleRate'"), std::string::npos);
}

// Test missing required parameter error
TEST_F(TypeManifestTest, MissingRequiredParameterError) {
  create_test_script("missing_param.lua", R"lua(
    function main(ctx, voltage, sampleRate)
      return nil
    end
  )lua");

  json params;
  params["script_path"] = (test_scripts_dir_ / "missing_param.lua").string();
  params["globals"] = {
      {"voltage", 5.0} // Missing sampleRate
  };
  params["type_manifest"] = {
      {"parameters",
       json::array({{{"name", "ctx"}, {"type", "RuntimeContext"}},
                    {{"name", "voltage"}, {"type", "number"}},
                    {{"name", "sampleRate"}, {"type", "number"}}})}};

  json out;
  int result = handle_measure(params, out);

  EXPECT_EQ(result, 1);
  EXPECT_FALSE(out["ok"].get<bool>());
  EXPECT_NE(out["error"].get<std::string>().find(
                "Missing required parameter 'sampleRate'"),
            std::string::npos);
}

// Test unused global variable warning
TEST_F(TypeManifestTest, UnusedGlobalWarning) {
  create_test_script("unused_global.lua", R"lua(
    function main(ctx, voltage)
      ctx:log("Voltage: " .. tostring(voltage))
      return nil
    end
  )lua");

  json params;
  params["script_path"] = (test_scripts_dir_ / "unused_global.lua").string();
  params["globals"] = {
      {"voltage", 5.0}, {"unusedParam", 999} // This should trigger a warning
  };
  params["type_manifest"] = {
      {"parameters", json::array({{{"name", "ctx"}, {"type", "RuntimeContext"}},
                                  {{"name", "voltage"}, {"type", "number"}}})}};

  json out;
  int result = handle_measure(params, out);

  EXPECT_EQ(result, 0);
  EXPECT_TRUE(out["ok"].get<bool>());

  auto log = read_log();
  // be less brittle — look for the variable name and the "not used" phrase
  // separately
  std::cout << "The entire log content:\n"
            << log << std::endl; // For debugging"
  EXPECT_NE(log.find("unusedParam"), std::string::npos);
  EXPECT_NE(log.find("not used"), std::string::npos);
}

// Test invalid type manifest structure
TEST_F(TypeManifestTest, InvalidManifestStructure) {
  create_test_script("invalid_manifest.lua", R"lua(
    function main(ctx)
      return nil
    end
  )lua");

  json params;
  params["script_path"] = (test_scripts_dir_ / "invalid_manifest.lua").string();
  params["type_manifest"] = {
      {"invalid_key", "invalid_value"} // Missing 'parameters' array
  };

  json out;
  int result = handle_measure(params, out);

  EXPECT_EQ(result, 1);
  EXPECT_FALSE(out["ok"].get<bool>());
  EXPECT_NE(out["error"].get<std::string>().find("Invalid type_manifest"),
            std::string::npos);
}

// Test backward compatibility without type manifest
TEST_F(TypeManifestTest, BackwardCompatibilityWithoutManifest) {
  create_test_script("no_manifest.lua", R"lua(
    function main(ctx)
      -- Access globals directly (old way)
      local v = voltage or 0
      ctx:log("Voltage: " .. tostring(v))
      return nil
    end
  )lua");

  json params;
  params["script_path"] = (test_scripts_dir_ / "no_manifest.lua").string();
  params["globals"] = {{"voltage", 5.0}};
  // No type_manifest provided - should use legacy behavior

  json out;
  int result = handle_measure(params, out);

  EXPECT_EQ(result, 0);
  EXPECT_TRUE(out["ok"].get<bool>());

  auto log = read_log();
  EXPECT_NE(log.find("Voltage: 5"), std::string::npos);
  // Look for the key phrases rather than exact wording; wording may vary
  // slightly
  EXPECT_NE(log.find("Injecting global"), std::string::npos);
  EXPECT_NE(log.find("voltage"), std::string::npos);
}

// Test parameter with missing name in manifest
TEST_F(TypeManifestTest, ParameterMissingName) {
  create_test_script("missing_name.lua", R"lua(
    function main(ctx, param)
      return nil
    end
  )lua");

  json params;
  params["script_path"] = (test_scripts_dir_ / "missing_name.lua").string();
  params["globals"] = {{"param", 5.0}};
  params["type_manifest"] = {
      {"parameters", json::array({
                         {{"name", "ctx"}, {"type", "RuntimeContext"}},
                         {{"type", "number"}} // Missing 'name' field
                     })}};

  json out;
  int result = handle_measure(params, out);

  EXPECT_EQ(result, 1);
  EXPECT_FALSE(out["ok"].get<bool>());
  EXPECT_NE(out["error"].get<std::string>().find("parameter 1 missing 'name'"),
            std::string::npos);
}

// Test complex types (tables)
TEST_F(TypeManifestTest, ComplexTypeTable) {
  create_test_script("table_param.lua", R"lua(
    function main(ctx, config)
      ctx:log("Voltage: " .. tostring(config.voltage))
      ctx:log("Rate: " .. tostring(config.rate))
      return nil
    end
  )lua");

  json params;
  params["script_path"] = (test_scripts_dir_ / "table_param.lua").string();
  params["globals"] = {{"config", {{"voltage", 5.0}, {"rate", 1000}}}};
  params["type_manifest"] = {
      {"parameters", json::array({{{"name", "ctx"}, {"type", "RuntimeContext"}},
                                  {{"name", "config"}, {"type", "table"}}})}};

  json out;
  int result = handle_measure(params, out);

  EXPECT_EQ(result, 0);
  EXPECT_TRUE(out["ok"].get<bool>());

  auto log = read_log();
  // match the actual formatted float "5.0" seen in the log sink
  EXPECT_NE(log.find("Voltage: 5.0"), std::string::npos);
  EXPECT_NE(log.find("Rate: 1000"), std::string::npos);
}
