#include "instrument-server/Logger.hpp"
#include "instrument-server/server/InstrumentRegistry.hpp"
#include "instrument-server/server/RuntimeContext.hpp"
#include "instrument-server/server/SyncCoordinator.hpp"
#include "instrument-server/server/CommandHandlers.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <sol/sol.hpp>
#include <spdlog/spdlog.h>
#include <thread>
#include <nlohmann/json.hpp>

using namespace instserver;
using namespace instserver::server;
using json = nlohmann::json;

class TypeManifestTest : public ::testing::Test {
protected:
  void SetUp() override {
    registry_ = &InstrumentRegistry::instance();
    registry_->stop_all();

    sync_coordinator_ = std::make_unique<SyncCoordinator>();

    auto tmp = std::filesystem::temp_directory_path();
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    log_path_ = tmp / ("instrument_test_" + std::to_string(now) + ".log");

    InstrumentLogger::instance().shutdown();
    InstrumentLogger::instance().init(log_path_.string(), spdlog::level::debug);

    if (auto l = spdlog::get("instrument")) {
      l->flush_on(spdlog::level::debug);
    }

    // Create temp directory for test scripts
    test_scripts_dir_ = tmp / "test_type_manifest_scripts";
    std::filesystem::create_directories(test_scripts_dir_);
  }

  void TearDown() override {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    if (auto l = spdlog::get("instrument")) {
      l->flush();
    }

    registry_->stop_all();
    InstrumentLogger::instance().shutdown();

    std::error_code ec;
    std::filesystem::remove(log_path_, ec);
    std::filesystem::remove_all(test_scripts_dir_, ec);
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
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    std::ifstream ifs(log_path_, std::ios::in | std::ios::binary);
    if (!ifs)
      return "";
    return std::string((std::istreambuf_iterator<char>(ifs)),
                       (std::istreambuf_iterator<char>()));
  }

  InstrumentRegistry *registry_{nullptr};
  std::unique_ptr<SyncCoordinator> sync_coordinator_;
  std::filesystem::path log_path_;
  std::filesystem::path test_scripts_dir_;
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
  params["globals"] = {
    {"voltage", 5.0},
    {"sampleRate", 1000}
  };
  params["type_manifest"] = {
    {"parameters", json::array({
      {{"name", "ctx"}, {"type", "RuntimeContext"}},
      {{"name", "voltage"}, {"type", "number"}},
      {{"name", "sampleRate"}, {"type", "number"}}
    })}
  };

  json out;
  int result = handle_measure(params, out);

  EXPECT_EQ(result, 0);
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
    {"voltage", 5.0}
    // Missing sampleRate
  };
  params["type_manifest"] = {
    {"parameters", json::array({
      {{"name", "ctx"}, {"type", "RuntimeContext"}},
      {{"name", "voltage"}, {"type", "number"}},
      {{"name", "sampleRate"}, {"type", "number"}}
    })}
  };

  json out;
  int result = handle_measure(params, out);

  EXPECT_EQ(result, 1);
  EXPECT_FALSE(out["ok"].get<bool>());
  EXPECT_NE(out["error"].get<std::string>().find("Missing required parameter 'sampleRate'"), 
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
    {"voltage", 5.0},
    {"unusedParam", 999}  // This should trigger a warning
  };
  params["type_manifest"] = {
    {"parameters", json::array({
      {{"name", "ctx"}, {"type", "RuntimeContext"}},
      {{"name", "voltage"}, {"type", "number"}}
    })}
  };

  json out;
  int result = handle_measure(params, out);

  EXPECT_EQ(result, 0);
  EXPECT_TRUE(out["ok"].get<bool>());

  auto log = read_log();
  EXPECT_NE(log.find("Global variable 'unusedParam' provided but not used"), std::string::npos);
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
    {"invalid_key", "invalid_value"}  // Missing 'parameters' array
  };

  json out;
  int result = handle_measure(params, out);

  EXPECT_EQ(result, 1);
  EXPECT_FALSE(out["ok"].get<bool>());
  EXPECT_NE(out["error"].get<std::string>().find("Invalid type_manifest"), std::string::npos);
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
  params["globals"] = {
    {"voltage", 5.0}
  };
  // No type_manifest provided - should use legacy behavior

  json out;
  int result = handle_measure(params, out);

  EXPECT_EQ(result, 0);
  EXPECT_TRUE(out["ok"].get<bool>());
  
  auto log = read_log();
  EXPECT_NE(log.find("Voltage: 5"), std::string::npos);
  // Should warn about global injection (legacy behavior)
  EXPECT_NE(log.find("Injecting global variable 'voltage'"), std::string::npos);
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
      {{"type", "number"}}  // Missing 'name' field
    })}
  };

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
  params["globals"] = {
    {"config", {
      {"voltage", 5.0},
      {"rate", 1000}
    }}
  };
  params["type_manifest"] = {
    {"parameters", json::array({
      {{"name", "ctx"}, {"type", "RuntimeContext"}},
      {{"name", "config"}, {"type", "table"}}
    })}
  };

  json out;
  int result = handle_measure(params, out);

  EXPECT_EQ(result, 0);
  EXPECT_TRUE(out["ok"].get<bool>());

  auto log = read_log();
  EXPECT_NE(log.find("Voltage: 5"), std::string::npos);
  EXPECT_NE(log.find("Rate: 1000"), std::string::npos);
}
