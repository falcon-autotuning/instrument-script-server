#include "PlatformPaths.hpp"
#include "PluginTestFixture.hpp"
#include "instrument-script-server/daemon/CommandHandlers.hpp"
#include "instrument-script-server/daemon/DataBufferManager.hpp"
#include "instrument-script-server/daemon/RuntimeContext.hpp"
#include "instrument-script-server/daemon/ServerDaemon.hpp"
#include <chrono>
#include <gtest/gtest.h>
#include <instrument-call-stack/instrument-call-stack-lua.h>
#include <instrument-log/inst_logging.h>
#include <numbers>
#include <thread>
constexpr double PI = std::numbers::pi;
namespace v1 = instserver::daemon::v1;
using namespace instserver;
using namespace instserver::daemon;
using namespace instserver::test;
#ifndef TEST_DATA_DIR
#define TEST_DATA_DIR "."
#endif
namespace {
constexpr std::array<std::string_view, 3> kWorkerLogs{
    "worker_MockInstrument1.log",
    "worker_MockInstrument2.log",
    "worker_MockInstrument3.log",
};
const std::array<std::filesystem::path, 4> kLogFiles{
    "script_test.log",
    kWorkerLogs[0],
    kWorkerLogs[1],
    kWorkerLogs[2],
};

LogContents read_inst1_log() { return read_log(kWorkerLogs[0]); }
LogContents read_inst2_log() { return read_log(kWorkerLogs[1]); }
LogContents read_inst3_log() { return read_log(kWorkerLogs[2]); }

bool local_read_buffer(const std::string &id, std::vector<double> &out_data,
                       uint64_t &out_count, uint32_t &out_type) {
  auto &mgr = instserver::daemon::DataBufferManager::instance();
  auto meta_opt = mgr.get_metadata(id);
  if (!meta_opt.has_value()) {
    return false;
  }

  DataBuffer *buf = data_manager_get_buffer(id.c_str());
  if (buf == nullptr) {
    return false;
  }

  void *data = data_buffer_data(buf);
  size_t n = data_buffer_element_count(buf);

  out_count = n;
  out_type = meta_opt->data_type();

  if (out_type == INST_DATA_FLOAT64) {
    const auto *ptr = static_cast<const double *>(data);
    out_data.assign(ptr, ptr + n);
  } else if (out_type == INST_DATA_FLOAT32) {
    const auto *ptr = static_cast<const float *>(data);
    out_data.assign(ptr, ptr + n);
  } else {
    data_manager_release_buffer(id.c_str());
    return false;
  }
  data_manager_release_buffer(id.c_str());
  return true;
}
} // namespace

class MeasurementScriptTest : public test::PluginTestFixture {
protected:
  void SetUp() override {
    PluginTestFixture::SetUp();
    inst_log_shutdown();
    log_path_ = kLogFiles[0];
    clear_test_logs(kLogFiles);
    inst_log_init(log_path_.string().c_str(), INST_LOG_DEBUG, "instrument",
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
  LogContents read_main_log() { return read_log(log_path_); }

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
  std::filesystem::path log_path_;
};

class TripleMeasurementScriptTest : public MeasurementScriptTest {
protected:
  void SetUp() override {
    MeasurementScriptTest::SetUp();
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
};

TEST_F(TripleMeasurementScriptTest, SimpleCall) {
  EXPECT_TRUE(run_script("simple_call.lua"));
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  auto main_log = read_main_log();
  main_log.does_not_contain_error();
  auto worker1_log = read_inst1_log();
  worker1_log.does_not_contain_error();
  auto worker2_log = read_inst2_log();
  worker2_log.does_not_contain_error();
  auto worker3_log = read_inst3_log();
  worker3_log.does_not_contain_error();
}

TEST_F(TripleMeasurementScriptTest, ParallelExecution) {
  EXPECT_TRUE(run_script("parallel_test.lua"));
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  auto main_log = read_main_log();
  main_log.does_not_contain_error();
  auto worker1_log = read_inst1_log();
  worker1_log.does_not_contain_error();
  auto worker2_log = read_inst2_log();
  worker2_log.does_not_contain_error();
  auto worker3_log = read_inst3_log();
  worker3_log.does_not_contain_error();
}

TEST_F(TripleMeasurementScriptTest, LoopMeasurement) {
  EXPECT_TRUE(run_script("loop_measurement.lua"));
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  auto main_log = read_main_log();
  main_log.does_not_contain_error();
  auto worker1_log = read_inst1_log();
  worker1_log.does_not_contain_error();
  auto worker2_log = read_inst2_log();
  worker2_log.does_not_contain_error();
  auto worker3_log = read_inst3_log();
  worker3_log.does_not_contain_error();
}

TEST_F(TripleMeasurementScriptTest, NestedParallel) {
  EXPECT_TRUE(run_script("nested_parallel.lua"));
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  auto main_log = read_main_log();
  main_log.does_not_contain_error();
  auto worker1_log = read_inst1_log();
  worker1_log.does_not_contain_error();
  auto worker2_log = read_inst2_log();
  worker2_log.does_not_contain_error();
  auto worker3_log = read_inst3_log();
  worker3_log.does_not_contain_error();
}

TEST_F(TripleMeasurementScriptTest, ErrorHandling) {
  // This script intentionally calls non-existent instrument
  // Should complete without crashing
  EXPECT_TRUE(run_script("error_handling.lua"));
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  auto main_log = read_main_log();
  main_log.contains_error();
  main_log.contains(
      "[LUA_CONTEXT] [CALL] Instrument not found: NonExistentInstrument");
  auto worker1_log = read_inst1_log();
  worker1_log.does_not_contain_error();
  auto worker2_log = read_inst2_log();
  worker2_log.does_not_contain_error();
  auto worker3_log = read_inst3_log();
  worker3_log.does_not_contain_error();
}

TEST_F(TripleMeasurementScriptTest, ReturnTypes) {
  EXPECT_TRUE(run_script("return_types.lua"));
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  auto main_log = read_main_log();
  main_log.does_not_contain_error();
  auto worker1_log = read_inst1_log();
  worker1_log.does_not_contain_error();
  auto worker2_log = read_inst2_log();
  worker2_log.does_not_contain_error();
  auto worker3_log = read_inst3_log();
  worker3_log.does_not_contain_error();
}

TEST_F(TripleMeasurementScriptTest, TableParameters) {
  EXPECT_TRUE(run_script("table_params.lua"));
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  auto main_log = read_main_log();
  main_log.does_not_contain_error();
  auto worker1_log = read_inst1_log();
  worker1_log.does_not_contain_error();
  auto worker2_log = read_inst2_log();
  worker2_log.does_not_contain_error();
  auto worker3_log = read_inst3_log();
  worker3_log.does_not_contain_error();
}

TEST_F(TripleMeasurementScriptTest, ScriptWithOutput) {
  auto *ctx = run_script_with_context("loop_measurement.lua");
  ASSERT_NE(ctx, nullptr);

  const auto &results = ctx->get_results();
  // Verify results were produced (script has measurements)
  EXPECT_FALSE(results.empty()) << "Script should produce measurement results";
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  auto main_log = read_main_log();
  main_log.does_not_contain_error();
  auto worker1_log = read_inst1_log();
  worker1_log.does_not_contain_error();
  auto worker2_log = read_inst2_log();
  worker2_log.does_not_contain_error();
  auto worker3_log = read_inst3_log();
  worker3_log.does_not_contain_error();
}

TEST_F(TripleMeasurementScriptTest, LargeBufferReturns) {
  auto plugin_path = get_test_plugin_path("mock_large_data_plugin");

  auto &plugin_reg = plugin::PluginRegistry::instance();
  try {
    plugin_reg.load_plugin("LargeMock", plugin_path.string());
  } catch (const std::exception &e) {
    GTEST_SKIP() << "Large data plugin not available: " << e.what();
  }

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
  type: Custom 
  name: LargeMock

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
  std::string test_scope_config = std::format(R"(
name: TestScope
api_ref: {}
connection:
  address: mock://testscope
)",
                                              api_path.string());

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
      std::vector<double> data;
      uint64_t count = 0;
      uint32_t dtype = 0;
      bool read_ok = local_read_buffer(results[0].returns[0].value.str_val,
                                       data, count, dtype);
      ASSERT_TRUE(read_ok);
      EXPECT_EQ(dtype, INST_DATA_FLOAT32);
      ASSERT_GE(data.size(), 100);
      for (size_t i = 0; i < 100; ++i) {
        double expected = std::sin(2.0 * PI * i / 100.0);
        EXPECT_NEAR(data[i], expected, 0.01);
      }
    }

    // 2. Recover second buffer and verify integrity
    {
      std::vector<double> data;
      uint64_t count = 0;
      uint32_t dtype = 0;
      bool read_ok = local_read_buffer(results[1].returns[0].value.str_val,
                                       data, count, dtype);
      ASSERT_TRUE(read_ok);
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
      v1::ReleaseBufferRequest release_req;
      release_req.set_buffer_id(results[0].returns[0].value.str_val);
      v1::ReleaseBufferResponse release_resp;
      int rc = handle_release_buffer(release_req, &release_resp);
      EXPECT_EQ(rc, 0);
      EXPECT_TRUE(release_resp.standard_response().ok());
    }

    {
      v1::ReleaseBufferRequest release_req;
      release_req.set_buffer_id(results[1].returns[0].value.str_val);
      v1::ReleaseBufferResponse release_resp;
      int rc = handle_release_buffer(release_req, &release_resp);
      EXPECT_EQ(rc, 0);
      EXPECT_TRUE(release_resp.standard_response().ok());
    }

    // 4. Validate that the buffers are gone after release
    {
      std::vector<double> data;
      uint64_t count = 0;
      uint32_t dtype = 0;
      bool read_ok = local_read_buffer(results[0].returns[0].value.str_val,
                                       data, count, dtype);
      EXPECT_FALSE(read_ok);
    }
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  auto main_log = read_main_log();
  main_log.does_not_contain_error();
  auto worker1_log = read_inst1_log();
  worker1_log.does_not_contain_error();
  auto worker2_log = read_inst2_log();
  worker2_log.does_not_contain_error();
  auto worker3_log = read_inst3_log();
  worker3_log.does_not_contain_error();

  // Clean up
  registry.remove_instrument("TestScope");
  std::filesystem::remove(config_path);
  std::filesystem::remove(api_path);
}

TEST_F(TripleMeasurementScriptTest, OuterMeasurePipelineWithMultipleBuffers) {
  auto &registry = InstrumentRegistry::instance();

  // Clean up any existing state
  registry.remove_instrument("TestScope");
  auto &manager = DataBufferManager::instance();
  manager.clear_all();

  // Path to mock plugin
  auto plugin_path = get_test_plugin_path("mock_large_data_plugin");
  if (!std::filesystem::exists(plugin_path)) {
    GTEST_SKIP() << "Mock VISA Large Data plugin not found at: " << plugin_path;
  }

  // Register plugin in global PluginRegistry first
  auto &plugin_reg = plugin::PluginRegistry::instance();
  try {
    plugin_reg.load_plugin("LargeMock", plugin_path.string());
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
  type: Custom 
  name: LargeMock

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
  v1::MeasureJobRequest measure_req;
  measure_req.set_script_path(script_path.string());
  v1::MeasureJobResultResponse measure_resp;

  int rc = handle_measure(measure_req, &measure_resp);
  ASSERT_EQ(rc, 0);

  const auto &results = measure_resp.results();
  // Should have 3 results: 2 large buffer calls + 1 small data call
  EXPECT_EQ(results.size(), 3);

  std::string buf1_id;
  std::string buf2_id;

  if (results.size() >= 2) {
    // Helper lambda to validate outer buffer structure and extract its ID
    auto validate_outer_buffer =
        [](const v1::CommandResult &result,
           const std::string &step_name) -> std::string {
      SCOPED_TRACE("Failure during payload validation: " + step_name);

      EXPECT_EQ(result.instrument_name(), "TestScope");
      EXPECT_EQ(result.verb(), "GET_LARGE_DATA");

      const auto &ret = result.param(0);

      EXPECT_EQ(ret.type(), v1::LUA_TYPES_DATA_BUFFER);
      EXPECT_FALSE(ret.value().s().empty());
      EXPECT_GT(ret.dbmeta().element_count(), 0ULL);
      EXPECT_EQ(ret.dbmeta().data_type(), INST_DATA_FLOAT32);

      return ret.value().s();
    };

    // Extract and validate both buffers cleanly
    buf1_id = validate_outer_buffer(results[0], "Buffer 0 Payload");
    buf2_id = validate_outer_buffer(results[1], "Buffer 1 Payload");

    EXPECT_NE(buf1_id, buf2_id)
        << "Error: Both results returned identical buffer IDs!";
  }

  if (results.size() >= 3) {
    // Validate third result (small data)
    const auto &r2 = results[2];
    EXPECT_EQ(r2.instrument_name(), "TestScope");
    EXPECT_EQ(r2.verb(), "GET_SMALL_DATA");
    EXPECT_EQ(r2.param(0).type(), v1::LUA_TYPES_DOUBLE);
  }
  // Helper lambda for clean, reusable buffer checking with detailed error
  // logging
  auto verify_buffer = [](const std::string &result, uint64_t expected_count,
                          const std::string &step_name) {
    SCOPED_TRACE("Failure context during step: " + step_name);

    std::vector<double> data;
    uint64_t count = 0;
    uint32_t dtype = 0;
    bool read_ok = local_read_buffer(result, data, count, dtype);

    ASSERT_TRUE(read_ok) << "local_read_buffer failed!";
    EXPECT_EQ(count, expected_count);
    EXPECT_EQ(dtype, INST_DATA_FLOAT32);

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

    v1::ReleaseBufferRequest release_req;
    release_req.set_buffer_id(buffer_id);
    v1::ReleaseBufferResponse release_resp;
    int release_rc = handle_release_buffer(release_req, &release_resp);

    ASSERT_EQ(release_rc, 0) << "handle_release_buffer failed!";
    EXPECT_TRUE(release_resp.standard_response().ok());
  };

  // Recover data and verify contents from the outermost context
  if (!buf1_id.empty() && !buf2_id.empty()) {
    verify_buffer(buf1_id, results[0].param(0).dbmeta().element_count(),
                  "First Buffer Verification");
    verify_buffer(buf2_id, results[1].param(0).dbmeta().element_count(),
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
      std::vector<double> data;
      uint64_t count = 0;
      uint32_t dtype = 0;
      bool read_ok = local_read_buffer(buf1_id, data, count, dtype);
      EXPECT_FALSE(read_ok);
    }
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  auto main_log = read_main_log();
  main_log.does_not_contain_error();
  auto worker1_log = read_inst1_log();
  worker1_log.does_not_contain_error();
  auto worker2_log = read_inst2_log();
  worker2_log.does_not_contain_error();
  auto worker3_log = read_inst3_log();
  worker3_log.does_not_contain_error();

  // Clean up
  registry.remove_instrument("TestScope");
  std::filesystem::remove(config_path);
  std::filesystem::remove(api_path);
}
class ConfigMeasurementScriptTest : public MeasurementScriptTest {
protected:
  void SetUp() override { MeasurementScriptTest::SetUp(); }
};

TEST_F(ConfigMeasurementScriptTest, ConfigInitialization) {
  auto &registry = InstrumentRegistry::instance();
  const auto config_path = test_configs_dir_ / "iss_config_test.yaml";
  const std::string addr = "VISA1::ADDR12";
  const int32_t baudrate = 12800;
  const std::string json = "{\"special\":5}";
  const int32_t delay = 5;
  const std::string init1 = "RST";
  const std::string init2 = "CLR";
  const std::string name = "MockInstrument1";

  std::ofstream config(config_path);
  config << std::format(R"yaml(
name: {}
api_ref: ./mock_api.yaml
connection:
  address: {}
  baudrate: {}
  custom: '{}'
startup:
  delay_ms: {}
  init_commands:
    - {}
    - {}
)yaml",
                        name, addr, baudrate, json, delay, init1, init2);
  config.close();

  try {
    registry.create_instrument(config_path.string());
  } catch (const std::exception &e) {
    GTEST_SKIP() << "Failed to create instrument: " << e.what();
  }
  EXPECT_TRUE(run_script("simple_call.lua"));

  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  auto main_log = read_main_log();
  main_log.does_not_contain_error();
  auto worker1_log = read_inst1_log();
  worker1_log.does_not_contain_error();
  worker1_log.contains(std::format("Initializing for {}", name));
  worker1_log.contains(std::format("The selected address: {}", addr));
  worker1_log.contains(std::format("The selected baud_rate: {}", baudrate));
  worker1_log.contains(std::format("The selected startup delay: {} ms", delay));
  worker1_log.contains(std::format("The custom string: {}", json));
  worker1_log.contains(std::format("The 0 command: {}", init1));
  worker1_log.contains(std::format("The 1 command: {}", init2));
  for (uint8_t i = 2; i < STARTUP_COMMANDS; i++) {
    worker1_log.contains(std::format("Empty init commands string at {}", i));
  }
  std::filesystem::remove(config_path);
}

TEST_F(ConfigMeasurementScriptTest, ConfigInitializationDefaults) {
  auto &registry = InstrumentRegistry::instance();
  const auto config_path = test_configs_dir_ / "iss_config_default_test.yaml";
  const std::string addr;
  const int32_t baudrate = 9600;
  const std::string json;
  const int32_t delay = 0;
  const std::string name = "MockInstrument1";

  std::ofstream config(config_path);
  config << std::format(R"yaml(
name: {}
api_ref: ./mock_api.yaml
)yaml",
                        name);
  config.close();

  try {
    registry.create_instrument(config_path.string());
  } catch (const std::exception &e) {
    GTEST_SKIP() << "Failed to create instrument: " << e.what();
  }
  EXPECT_TRUE(run_script("simple_call.lua"));

  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  auto main_log = read_main_log();
  main_log.does_not_contain_error();
  auto worker1_log = read_inst1_log();
  worker1_log.does_not_contain_error();
  worker1_log.contains(std::format("Initializing for {}", name));
  worker1_log.contains(std::format("The selected address: {}", addr));
  worker1_log.contains(std::format("The selected baud_rate: {}", baudrate));
  worker1_log.contains(std::format("The selected startup delay: {} ms", delay));
  worker1_log.contains(std::format("The custom string: {}", json));
  for (uint8_t i = 0; i < STARTUP_COMMANDS; i++) {
    worker1_log.contains(std::format("Empty init commands string at {}", i));
  }
  std::filesystem::remove(config_path);
}

TEST_F(ConfigMeasurementScriptTest, ProperVISACommands) {
  auto plugin_path = get_test_plugin_path("mock_plugin");

  auto &plugin_reg = plugin::PluginRegistry::instance();
  try {
    plugin_reg.load_plugin("VISA", plugin_path.string());
  } catch (const std::exception &e) {
    GTEST_SKIP() << "Plugin not available: " << e.what();
  }
  auto &registry = InstrumentRegistry::instance();
  const auto config_path = test_configs_dir_ / "iss_config_default_test.yaml";
  const std::string name = "MockInstrument1";

  std::ofstream config(config_path);
  config << std::format(R"yaml(
name: {}
api_ref: ./mock_visa_api.yaml
)yaml",
                        name);
  config.close();

  try {
    registry.create_instrument(config_path.string());
  } catch (const std::exception &e) {
    GTEST_SKIP() << "Failed to create instrument: " << e.what();
  }
  EXPECT_TRUE(run_script("table_params.lua"));

  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  auto main_log = read_main_log();
  main_log.does_not_contain_error();
  auto worker1_log = read_inst1_log();
  worker1_log.does_not_contain_error();
  worker1_log.contains("The command selected is 'CONF 1.5,test,ON'");
  worker1_log.contains("The command selected is 'CONF 2.0,test,ON'");
  worker1_log.contains("The command selected is 'CONF -2.0,test,ON'");

  std::filesystem::remove(config_path);
}
