#include "PluginTestFixture.hpp"
#include "instrument-server/Logger.hpp"
#include "instrument-server/server/InstrumentRegistry.hpp"
#include "instrument-server/server/RuntimeContext.hpp"
#include "instrument-server/server/ServerDaemon.hpp"
#include "instrument-server/server/SyncCoordinator.hpp"
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <sol/sol.hpp>

using namespace instserver;
using json = nlohmann::json;

// Helper function to validate JSON structure for measurement results
bool validate_measurement_results_json(const json &j, std::string &error) {
  // Check required top-level fields
  if (!j.contains("status")) {
    error = "Missing 'status' field";
    return false;
  }
  if (!j.contains("script")) {
    error = "Missing 'script' field";
    return false;
  }
  if (!j.contains("results")) {
    error = "Missing 'results' field";
    return false;
  }
  
  // Validate status
  if (!j["status"].is_string()) {
    error = "'status' must be a string";
    return false;
  }
  std::string status = j["status"];
  if (status != "success" && status != "error") {
    error = "'status' must be 'success' or 'error'";
    return false;
  }
  
  // Validate script
  if (!j["script"].is_string()) {
    error = "'script' must be a string";
    return false;
  }
  
  // Validate results array
  if (!j["results"].is_array()) {
    error = "'results' must be an array";
    return false;
  }
  
  // Validate each result in the array
  int idx = 0;
  for (const auto &result : j["results"]) {
    std::string prefix = "results[" + std::to_string(idx) + "]: ";
    
    // Check required fields
    if (!result.contains("index")) {
      error = prefix + "Missing 'index' field";
      return false;
    }
    if (!result.contains("instrument")) {
      error = prefix + "Missing 'instrument' field";
      return false;
    }
    if (!result.contains("verb")) {
      error = prefix + "Missing 'verb' field";
      return false;
    }
    if (!result.contains("params")) {
      error = prefix + "Missing 'params' field";
      return false;
    }
    if (!result.contains("executed_at_ms")) {
      error = prefix + "Missing 'executed_at_ms' field";
      return false;
    }
    if (!result.contains("return")) {
      error = prefix + "Missing 'return' field";
      return false;
    }
    
    // Validate types
    if (!result["index"].is_number_integer()) {
      error = prefix + "'index' must be an integer";
      return false;
    }
    if (!result["instrument"].is_string()) {
      error = prefix + "'instrument' must be a string";
      return false;
    }
    if (!result["verb"].is_string()) {
      error = prefix + "'verb' must be a string";
      return false;
    }
    if (!result["params"].is_object()) {
      error = prefix + "'params' must be an object";
      return false;
    }
    if (!result["executed_at_ms"].is_number_integer()) {
      error = prefix + "'executed_at_ms' must be an integer";
      return false;
    }
    
    // Validate return object
    const auto &ret = result["return"];
    if (!ret.is_object()) {
      error = prefix + "'return' must be an object";
      return false;
    }
    if (!ret.contains("type")) {
      error = prefix + "'return' must have a 'type' field";
      return false;
    }
    if (!ret["type"].is_string()) {
      error = prefix + "'return.type' must be a string";
      return false;
    }
    
    std::string ret_type = ret["type"];
    
    // For buffer type, check required buffer fields
    if (ret_type == "buffer") {
      if (!ret.contains("buffer_id") || !ret["buffer_id"].is_string()) {
        error = prefix + "buffer return must have 'buffer_id' (string)";
        return false;
      }
      if (!ret.contains("element_count") || !ret["element_count"].is_number_integer()) {
        error = prefix + "buffer return must have 'element_count' (integer)";
        return false;
      }
      if (!ret.contains("data_type") || !ret["data_type"].is_string()) {
        error = prefix + "buffer return must have 'data_type' (string)";
        return false;
      }
    } else if (ret_type != "void") {
      // For non-void, non-buffer types, should have value field
      if (!ret.contains("value")) {
        error = prefix + "non-void return must have 'value' field";
        return false;
      }
    }
    
    idx++;
  }
  
  return true;
}

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
  EXPECT_EQ(results[0].verb, "GET_DOUBLE");
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

TEST_F(MeasurementScriptTest, LargeBufferReturns) {
  // This test requires mock_visa_large_data_plugin
  // Load the special test scope configuration
  auto &registry = InstrumentRegistry::instance();
  
  // Create a test configuration for TestScope that uses the large data plugin
  std::string test_scope_config = R"(
instrument:
  name: TestScope
  connection:
    protocol: VISA
    address: "mock://testscope"
  api: tests/data/mock_api.yaml
  plugin: ./build/tests/mock_visa_large_data_plugin.so
)";
  
  // Write config to temporary file
  std::string config_path = "/tmp/test_scope_large_data.yaml";
  std::ofstream config_file(config_path);
  config_file << test_scope_config;
  config_file.close();
  
  // Start the TestScope instrument
  try {
    registry.create_instrument(config_path);
  } catch (const std::exception &e) {
    // Plugin might not be available
    GTEST_SKIP() << "Large data plugin not available: " << e.what();
  }
  
  auto ctx = run_script_with_context("large_buffer_returns.lua");
  ASSERT_NE(ctx, nullptr);
  
  const auto &results = ctx->get_results();
  
  // Should have 3 results: 2 large buffer calls + 1 small data call
  EXPECT_EQ(results.size(), 3);
  
  // First two results should be buffer references
  if (results.size() >= 2) {
    EXPECT_TRUE(results[0].has_large_data);
    EXPECT_FALSE(results[0].buffer_id.empty());
    EXPECT_GT(results[0].element_count, 0);
    EXPECT_EQ(results[0].return_type, "buffer");
    
    EXPECT_TRUE(results[1].has_large_data);
    EXPECT_FALSE(results[1].buffer_id.empty());
    EXPECT_GT(results[1].element_count, 0);
    EXPECT_EQ(results[1].return_type, "buffer");
    
    // Buffer IDs should be different
    EXPECT_NE(results[0].buffer_id, results[1].buffer_id);
  }
  
  // Third result should be regular return value
  if (results.size() >= 3) {
    EXPECT_FALSE(results[2].has_large_data);
    EXPECT_TRUE(results[2].return_value.has_value());
  }
  
  // Clean up
  registry.remove_instrument("TestScope");
  std::remove(config_path.c_str());
}

TEST_F(MeasurementScriptTest, JSONOutputValidation) {
  // Test that JSON output conforms to the expected schema structure
  auto ctx = run_script_with_context("multiple_returns.lua");
  ASSERT_NE(ctx, nullptr);
  
  const auto &results = ctx->get_results();
  ASSERT_GT(results.size(), 0);
  
  // Build JSON output manually (simulating what instrument_server_main.cpp does)
  json output;
  output["status"] = "success";
  output["script"] = "multiple_returns.lua";
  output["results"] = json::array();
  
  for (size_t i = 0; i < results.size(); ++i) {
    const auto &r = results[i];
    json result_json;
    
    result_json["index"] = i;
    result_json["instrument"] = r.instrument_name;
    result_json["verb"] = r.verb;
    
    // Add params
    json params_json = json::object();
    for (const auto &[key, value] : r.params) {
      if (std::holds_alternative<double>(value)) {
        params_json[key] = std::get<double>(value);
      } else if (std::holds_alternative<int64_t>(value)) {
        params_json[key] = std::get<int64_t>(value);
      } else if (std::holds_alternative<std::string>(value)) {
        params_json[key] = std::get<std::string>(value);
      } else if (std::holds_alternative<bool>(value)) {
        params_json[key] = std::get<bool>(value);
      }
    }
    result_json["params"] = params_json;
    
    // Add timestamp (using a placeholder for this test)
    result_json["executed_at_ms"] = 1704720615123 + i;
    
    // Add return value
    json return_json;
    if (r.has_large_data) {
      return_json["type"] = "buffer";
      return_json["buffer_id"] = r.buffer_id;
      return_json["element_count"] = r.element_count;
      return_json["data_type"] = r.data_type;
    } else if (r.return_value) {
      return_json["type"] = r.return_type;
      
      if (std::holds_alternative<double>(*r.return_value)) {
        return_json["value"] = std::get<double>(*r.return_value);
      } else if (std::holds_alternative<int64_t>(*r.return_value)) {
        return_json["value"] = std::get<int64_t>(*r.return_value);
      } else if (std::holds_alternative<std::string>(*r.return_value)) {
        return_json["value"] = std::get<std::string>(*r.return_value);
      } else if (std::holds_alternative<bool>(*r.return_value)) {
        return_json["value"] = std::get<bool>(*r.return_value);
      } else if (std::holds_alternative<std::vector<double>>(*r.return_value)) {
        return_json["value"] = std::get<std::vector<double>>(*r.return_value);
      }
    } else {
      return_json["type"] = r.return_type;
      return_json["value"] = nullptr;
    }
    
    result_json["return"] = return_json;
    output["results"].push_back(result_json);
  }
  
  // Validate the JSON structure
  std::string error;
  bool is_valid = validate_measurement_results_json(output, error);
  
  EXPECT_TRUE(is_valid) << "JSON validation failed: " << error;
  
  // Additional checks
  EXPECT_EQ(output["status"], "success");
  EXPECT_EQ(output["script"], "multiple_returns.lua");
  EXPECT_TRUE(output["results"].is_array());
  EXPECT_EQ(output["results"].size(), results.size());
  
  // Verify JSON can be serialized
  std::string json_str = output.dump(2);
  EXPECT_FALSE(json_str.empty());
  
  // Verify JSON can be parsed back
  json parsed = json::parse(json_str);
  EXPECT_EQ(parsed, output);
}

