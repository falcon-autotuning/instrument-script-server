#include "PlatformPaths.hpp"
#include "PluginTestFixture.hpp"
#include "instrument-script-server/ipc/DataBufferManager.hpp"
#include "instrument-script-server/plugin/PluginRegistry.hpp"
#include "instrument-script-server/server/CommandHandlers.hpp"
#include "instrument-script-server/server/InstrumentRegistry.hpp"
#include "instrument-script-server/server/RuntimeContext.hpp"
#include "instrument-script-server/server/ServerDaemon.hpp"
#include "instrument-script-server/server/SyncCoordinator.hpp"
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
constexpr double PI = 3.14159265358979323846;
using namespace instserver;
using namespace instserver::test;
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
    std::string prefix = "results[" + std::to_string(idx) + "]:  ";

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
      if (!ret.contains("element_count") ||
          !ret["element_count"].is_number_integer()) {
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
    inst_log_shutdown();
    inst_log_init("script_test.log", INST_LOG_DEBUG, "instrument",
                  1024 * 1024, // 1 MB
                  3);          // rotation count

    test_scripts_dir_ =
        std::filesystem::current_path() / "data" / "test_scripts";
    test_configs_dir_ = std::filesystem::current_path() / "data";

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
    // Clean up after each test - use public API only
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

      // 1️⃣ load script
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

      // Use member context
      test_context_ = std::make_unique<RuntimeContext>(
          registry, ServerDaemon::instance().sync_coordinator());
      lua["context"] = test_context_.get();

      // 1️⃣ load script
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
  auto *ctx = run_script_with_context("loop_measurement.lua");
  ASSERT_NE(ctx, nullptr);

  const auto &results = ctx->get_results();
  // Verify results were produced (script has measurements)
  EXPECT_FALSE(results.empty()) << "Script should produce measurement results";
}

TEST_F(MeasurementScriptTest, MultipleReturns) {
  auto *ctx = run_script_with_context("multiple_returns.lua");
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

TEST_F(MeasurementScriptTest, ChannelAddressingWithReturns) {
  auto *ctx = run_script_with_context("channel_addressing.lua");
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

TEST_F(MeasurementScriptTest, LargeBufferReturns) {
  // FIXED: Use cross-platform plugin path
  auto plugin_path = get_test_plugin_path("mock_visa_large_data_plugin");

  auto &plugin_reg = plugin::PluginRegistry::instance();
  try {
    plugin_reg.load_plugin("VISA_LARGE", plugin_path.string());
  } catch (const std::exception &e) {
    GTEST_SKIP() << "Large data plugin not available: " << e.what();
  }

  // FIXED: Use cross-platform temp directory
  auto temp_dir = std::filesystem::temp_directory_path();

  // Create a modified API file with VISA_LARGE protocol
  std::string api_content = R"(
api_version:  "1.0.0"
instrument: 
  vendor: "MockVendor"
  model: "MockModel"
  identifier: "MockAPI"
  description: "Mock instrument for testing"

protocol:
  type: VISA_LARGE

io:
  - name: waveform
    type: array
    role: output
    description: "Array of measurements"
  - name: current
    type: float
    role:  output
    description: "Measured current"
    unit: "A"

commands:
  GET_LARGE_DATA:
    template: "WAVEFORM:DATA?"
    description: "Get large waveform data"
    parameters:  []
    outputs: [waveform]

  GET_SMALL_DATA:
    template: "DATA:SMALL?"
    description: "Get small data value"
    parameters: []
    outputs: [current]
)";

  std::filesystem::path api_path = temp_dir / "mock_api_large.yaml";
  std::ofstream api_file(api_path);
  api_file << api_content;
  api_file.close();

  // Load the special test scope configuration
  auto &registry = InstrumentRegistry::instance();

  // Create a test configuration for TestScope that uses the large data plugin
  std::string test_scope_config = "name: TestScope\n"
                                  "api_ref: " +
                                  api_path.string() +
                                  "\n"
                                  "connection:\n"
                                  "  type: VISA_LARGE\n"
                                  "  address: \"mock://testscope\"\n";

  // Write config to temporary file
  std::filesystem::path config_path = temp_dir / "test_scope_large_data.yaml";
  std::ofstream config_file(config_path);
  config_file << test_scope_config;
  config_file.close();

  // Start the TestScope instrument
  try {
    registry.create_instrument(config_path.string());
  } catch (const std::exception &e) {
    // Plugin might not be available
    GTEST_SKIP() << "Failed to create instrument: " << e.what();
  }

  auto *ctx = run_script_with_context("large_buffer_returns.lua");
  ASSERT_NE(ctx, nullptr);

  const auto &results = ctx->get_results();

  // Should have 3 results: 2 large buffer calls + 1 small data call
  EXPECT_EQ(results.size(), 3);

  // First two results should be buffer references
  if (results.size() >= 2) {
    EXPECT_EQ(results[0].returns[0].type, PARAM_TYPE_BUFFER);
    EXPECT_EQ(results[1].returns[0].type, PARAM_TYPE_BUFFER);

    // Buffer IDs should be different
    EXPECT_NE(results[0].returns[0].value.str_val,
              results[1].returns[0].value.str_val);
  }

  // Third result should be regular return value
  if (results.size() >= 3) {
    EXPECT_EQ(results[2].returns[0].type, PARAM_TYPE_DOUBLE);
  }

  // Robust Integration Test / Data Recovery Verification:
  // Ensure that the output data inside the buffers is recovered successfully
  // from the outermost context.
  if (results.size() >= 2) {
    // 1. Recover first buffer and verify integrity (sin wave)
    {
      nlohmann::json read_params, read_out;
      read_params["buffer_id"] = results[0].returns[0].value.str_val;
      int rc = server::handle_read_buffer(read_params, read_out);
      if (rc != 0) {
        std::string error_msg = read_out.contains("error")
                                    ? read_out["error"].dump()
                                    : "No error key found";
        FAIL() << "handle_read_buffer failed with rc=" << rc
               << ". Details: " << error_msg;
      }
      EXPECT_TRUE(read_out.value("ok", false));
      EXPECT_EQ(read_out["data_type"], INST_DATA_FLOAT64);

      auto data = read_out["data"].get<std::vector<double>>();
      ASSERT_GE(data.size(), 100);
      for (size_t i = 0; i < 100; ++i) {
        double expected = std::sin(2.0 * PI * i / 100.0);
        EXPECT_NEAR(data[i], expected, 0.01);
      }
    }

    // 2. Recover second buffer and verify integrity
    {
      nlohmann::json read_params, read_out;
      read_params["buffer_id"] = results[1].returns[0].value.str_val;
      int rc = server::handle_read_buffer(read_params, read_out);
      ASSERT_EQ(rc, 0);
      EXPECT_TRUE(read_out.value("ok", false));

      auto data = read_out["data"].get<std::vector<double>>();
      ASSERT_GE(data.size(), 100);
      for (size_t i = 0; i < 100; ++i) {
        double expected = std::sin(2.0 * PI * i / 100.0);
        EXPECT_NEAR(data[i], expected, 0.01);
      }
    }

    // 3. Ownership & Handoff validation:
    //    We explicitly call release_buffer on the server, which should
    //    decrement the refcount to 0 (since the worker's owner reference was
    //    already gracefully transferred via our hand-off protocol), causing the
    //    shared memory buffer to be deallocated cleanly.
    {
      nlohmann::json release_params, release_out;
      release_params["buffer_id"] = results[0].returns[0].value.str_val;
      int rc = server::handle_release_buffer(release_params, release_out);
      EXPECT_EQ(rc, 0);
      EXPECT_TRUE(release_out.value("ok", false));
    }

    {
      nlohmann::json release_params, release_out;
      release_params["buffer_id"] = results[1].returns[0].value.str_val;
      int rc = server::handle_release_buffer(release_params, release_out);
      EXPECT_EQ(rc, 0);
      EXPECT_TRUE(release_out.value("ok", false));
    }

    // 4. Validate that the buffers are gone after release
    {
      nlohmann::json read_params, read_out;
      read_params["buffer_id"] = results[0].returns[0].value.str_val;
      int rc = server::handle_read_buffer(read_params, read_out);
      EXPECT_FALSE(read_out.value("ok", false));
    }
  }

  // Clean up
  registry.remove_instrument("TestScope");
  std::filesystem::remove(config_path);
  std::filesystem::remove(api_path);
}

TEST_F(MeasurementScriptTest, OuterMeasurePipelineWithMultipleBuffers) {
  auto &registry = InstrumentRegistry::instance();

  // Clean up any existing state
  registry.remove_instrument("TestScope");
  auto &manager = ipc::DataBufferManager::instance();
  manager.clear_all();

  // Path to mock plugin
  auto plugin_path = get_test_plugin_path("mock_visa_large_data_plugin");
  if (!std::filesystem::exists(plugin_path)) {
    GTEST_SKIP() << "Mock VISA Large Data plugin not found at: " << plugin_path;
  }

  // Register plugin in global PluginRegistry first
  auto &plugin_reg = plugin::PluginRegistry::instance();
  try {
    plugin_reg.load_plugin("VISA_LARGE", plugin_path.string());
  } catch (const std::exception &e) {
    GTEST_SKIP() << "Large data plugin not available: " << e.what();
  }

  // Create temporary API definition file
  auto temp_dir = std::filesystem::temp_directory_path();
  std::filesystem::path api_path = temp_dir / "mock_api_large_data.yaml";
  std::ofstream api_file(api_path);

  std::string api_content = R"(
api_version: "1.0.0"
instrument:
  vendor: "MockVendor"
  model: "MockModel"
  identifier: "MockAPI"
  description: "Mock instrument for testing"

protocol:
  type: VISA_LARGE

io:
  - name: waveform
    type: array
    role: output
    description: "Array of measurements"
  - name: current
    type: float
    role: output
    description: "Measured current"
    unit: "A"

commands:
  GET_LARGE_DATA:
    template: "WAVEFORM:DATA?"
    description: "Get large waveform data"
    parameters: []
    outputs: [waveform]

  GET_SMALL_DATA:
    template: "DATA:SMALL?"
    description: "Get small data value"
    parameters: []
    outputs: [current]
)";

  api_file << api_content;
  api_file.close();

  // Create mock scope config identical to working LargeBufferReturns test
  std::string test_scope_config = "name: TestScope\n"
                                  "api_ref: " +
                                  api_path.string() +
                                  "\n"
                                  "connection:\n"
                                  "  type: VISA_LARGE\n"
                                  "  address: \"mock://testscope\"\n";

  std::filesystem::path config_path = temp_dir / "test_scope_large_data.yaml";
  std::ofstream config_file(config_path);
  config_file << test_scope_config;
  config_file.close();

  // Start the TestScope instrument
  try {
    registry.create_instrument(config_path.string());
  } catch (const std::exception &e) {
    GTEST_SKIP() << "Failed to create instrument: " << e.what();
  }

  // Locate the lua script using the test_scripts_dir_ member from the fixture
  auto script_path = test_scripts_dir_ / "large_buffer_returns.lua";

  // Call the outermost measurement RPC handler: server::handle_measure!
  nlohmann::json params, out;
  params["script_path"] = script_path.string();
  params["json"] = true; // Request detailed json results format

  int rc = server::handle_measure(params, out);
  ASSERT_EQ(rc, 0);
  EXPECT_TRUE(out.value("ok", false));
  EXPECT_EQ(out.value("script", ""), "large_buffer_returns.lua");

  ASSERT_TRUE(out.contains("results"));
  ASSERT_TRUE(out["results"].is_array());

  const auto &results = out["results"];
  // Should have 3 results: 2 large buffer calls + 1 small data call
  EXPECT_EQ(results.size(), 3);

  std::string buf1_id;
  std::string buf2_id;

  if (results.size() >= 2) {
    // Helper lambda to validate outer buffer structure and extract its ID
    auto validate_outer_buffer =
        [](const nlohmann::json &result, int expected_index,
           const std::string &step_name) -> std::string {
      SCOPED_TRACE("Failure during payload validation: " + step_name);

      EXPECT_EQ(result.value("index", -1), expected_index);
      EXPECT_EQ(result.value("instrument", ""), "TestScope");
      EXPECT_EQ(result.value("verb", ""), "GET_LARGE_DATA");

      EXPECT_TRUE(result.contains("return"));
      const auto &ret = result["return"];

      EXPECT_EQ(ret.value("type", ""), "buffer");
      EXPECT_FALSE(ret.value("buffer_id", "").empty());
      EXPECT_GT(ret.value("element_count", 0ULL), 0ULL);
      EXPECT_EQ(ret.value("data_type", ""), "float32");

      return ret.value("buffer_id", "");
    };

    // Extract and validate both buffers cleanly
    buf1_id = validate_outer_buffer(results[0], 0, "Buffer 0 Payload");
    buf2_id = validate_outer_buffer(results[1], 1, "Buffer 1 Payload");

    EXPECT_NE(buf1_id, buf2_id)
        << "Error: Both results returned identical buffer IDs!";
  }

  if (results.size() >= 3) {
    // Validate third result (small data)
    const auto &r2 = results[2];
    EXPECT_EQ(r2.value("index", -1), 2);
    EXPECT_EQ(r2.value("instrument", ""), "TestScope");
    EXPECT_EQ(r2.value("verb", ""), "GET_SMALL_DATA");
    ASSERT_TRUE(r2.contains("return"));
    EXPECT_EQ(r2["return"].value("type", ""), "float");
    EXPECT_TRUE(r2["return"].contains("value"));
  }
  // Helper lambda for clean, reusable buffer checking with detailed error
  // logging
  auto verify_buffer = [](const auto &result, uint64_t expected_count,
                          const std::string &step_name) {
    SCOPED_TRACE("Failure context during step: " + step_name);

    nlohmann::json read_params, read_out;
    read_params["buffer_id"] = result;

    int rc = server::handle_read_buffer(read_params, read_out);

    // If this assertion fails, SCOPED_TRACE will dump the step_name and the
    // JSON error context automatically
    ASSERT_EQ(rc, 0) << "handle_read_buffer failed! JSON details: "
                     << read_out.value("error", "No error key found");

    EXPECT_TRUE(read_out.value("ok", false));
    EXPECT_EQ(read_out.value("element_count", 0ULL), expected_count);
    EXPECT_EQ(read_out["data_type"], INST_DATA_FLOAT64);

    auto data = read_out["data"].template get<std::vector<double>>();
    ASSERT_GE(data.size(), 100);
    for (size_t i = 0; i < 100; ++i) {
      double expected = std::sin(2.0 * PI * i / 100.0);
      EXPECT_NEAR(data[i], expected, 0.01);
    }
  };
  // Helper lambda for clean, reusable buffer release and verification
  auto release_and_verify = [](const std::string &buffer_id,
                               const std::string &step_name) {
    SCOPED_TRACE("Failure context during release: " + step_name);

    nlohmann::json release_params, release_out;
    release_params["buffer_id"] = buffer_id;

    int release_rc = server::handle_release_buffer(release_params, release_out);

    ASSERT_EQ(release_rc, 0)
        << "handle_release_buffer failed! Details: "
        << release_out.value("error", "No error key found");
    EXPECT_TRUE(release_out.value("ok", false));
  };

  // Recover data and verify contents from the outermost context
  if (!buf1_id.empty() && !buf2_id.empty()) {
    verify_buffer(buf1_id, results[0]["return"]["element_count"],
                  "First Buffer Verification");
    verify_buffer(buf2_id, results[1]["return"]["element_count"],
                  "Second Buffer Verification");

    // 3. Ownership & Handoff validation:
    //    We explicitly call release_buffer on the server, which should
    //    decrement the refcount to 0 (since the worker's owner reference was
    //    already gracefully transferred via our hand-off protocol), causing the
    //    shared memory buffer to be deallocated cleanly.
    release_and_verify(buf1_id, "Release Buffer 1");
    release_and_verify(buf2_id, "Release Buffer 2");

    // 4. Validate that the buffers are gone after release
    {
      nlohmann::json read_params, read_out;
      read_params["buffer_id"] = buf1_id;
      int read_rc = server::handle_read_buffer(read_params, read_out);
      EXPECT_FALSE(read_out.value("ok", false));
    }
  }

  // Clean up
  registry.remove_instrument("TestScope");
  std::filesystem::remove(config_path);
  std::filesystem::remove(api_path);
}
