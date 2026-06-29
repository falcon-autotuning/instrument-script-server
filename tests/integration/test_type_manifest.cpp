#include "PluginTestFixture.hpp"
#include "instrument-script-server/daemon/CommandHandlers.hpp"
#include "instrument-script-server/daemon/InstrumentRegistry.hpp"
#include "instrument-script-server/daemon/PluginRegistry.hpp"
#include "instrument-script-server/daemon/RuntimeContext.hpp"
#include "instrument-script-server/daemon/ServerDaemon.hpp"
#include "instrument-script-server/daemon/SyncCoordinator.hpp"
#include <instrument-log/inst_logging.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <sol/sol.hpp>
#include <spdlog/spdlog.h>
#include <thread>

#include <google/protobuf/util/json_util.h>
using namespace instserver;
using namespace instserver::daemon;
#ifndef TEST_DATA_DIR
#define TEST_DATA_DIR "."
#endif

class TypeManifestTest : public test::PluginTestFixture {
protected:
  void SetUp() override {
    log_path_ = std::filesystem::current_path() / "script_test.log";
    PluginTestFixture::SetUp();
    inst_log_shutdown();
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

    // Start mock instruments
    auto &registry = InstrumentRegistry::instance();

    std::filesystem::path config1 = test_configs_dir_ / "mock_instrument1.yaml";
    std::filesystem::path config2 = test_configs_dir_ / "mock_instrument2.yaml";
    std::filesystem::path config3 = test_configs_dir_ / "mock_instrument3.yaml";

    if (std::filesystem::exists(config1)) {
      registry.create_instrument(config1.string());
    }
    if (std::filesystem::exists(config2)) {
      registry.create_instrument(config2.string());
    }
    if (std::filesystem::exists(config3)) {
      registry.create_instrument(config3.string());
    }

    // Create temp directory for test scripts
    std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "test_type_manifest_scripts";
    std::filesystem::create_directories(tmp);
  }

  void TearDown() override {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    auto &registry = InstrumentRegistry::instance();
    registry.stop_all();
    auto &plugin_registry = instserver::plugin::PluginRegistry::instance();
    plugin_registry.unload_all();

    std::error_code ec;
    std::filesystem::remove(log_path_, ec);

    inst_log_flush();
    inst_log_shutdown();
    // Clean up after each test - use public API only
    auto &daemon = ServerDaemon::instance();
    if (daemon.is_running()) {
      daemon.stop();
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
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
        return {std::istreambuf_iterator<char>(ifs),
                std::istreambuf_iterator<char>()};
      }
    }
    // Final attempt
    std::ifstream ifs(log_path_, std::ios::in | std::ios::binary);
    if (!ifs) {
      return "";
    }
    return {std::istreambuf_iterator<char>(ifs),
            std::istreambuf_iterator<char>()};
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

  MeasureJobRequest req{};
  req.set_script_path((test_scripts_dir_ / "typed_main.lua").string());

  auto *globals = req.mutable_globals();
  auto *map = globals->mutable_map();

  VariableValue voltage_val;
  voltage_val.set_d(5.0);
  (*map)["voltage"] = voltage_val;

  VariableValue sample_rate_val;
  sample_rate_val.set_i(1000);
  (*map)["sampleRate"] = sample_rate_val;
  auto *type_manifest = req.mutable_type_manifest();
  auto *parameters = type_manifest->mutable_parameters();
  {
    auto *p = parameters->Add();
    p->set_name("voltage");
    p->set_type(LUA_TYPES_DOUBLE);
  }
  {
    auto *p = parameters->Add();
    p->set_name("sampleRate");
    p->set_type(LUA_TYPES_INT64);
  }

  MeasureJobResultResponse resp{};
  int result = handle_measure(req, &resp);

  EXPECT_EQ(result, 0);
  if (!resp.standard_response().ok()) {
    std::cout << resp.standard_response().error().message() << '\n';
  }
  EXPECT_TRUE(resp.standard_response().ok());

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

  MeasureJobRequest req{};
  req.set_script_path((test_scripts_dir_ / "missing_param.lua").string());
  auto *globals = req.mutable_globals();
  auto *map = globals->mutable_map();

  VariableValue voltage_val;
  voltage_val.set_d(5.0);
  (*map)["voltage"] = voltage_val;
  // Missing sampleRate
  auto *type_manifest = req.mutable_type_manifest();
  auto *parameters = type_manifest->mutable_parameters();
  {
    auto *p = parameters->Add();
    p->set_name("voltage");
    p->set_type(LUA_TYPES_DOUBLE);
  }
  {
    auto *p = parameters->Add();
    p->set_name("sampleRate");
    p->set_type(LUA_TYPES_INT64);
  }

  MeasureJobResultResponse resp{};
  int result = handle_measure(req, &resp);

  EXPECT_EQ(result, 1);
  EXPECT_FALSE(resp.standard_response().ok());
  EXPECT_NE(resp.standard_response().error().message().find(
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

  MeasureJobRequest req{};
  req.set_script_path((test_scripts_dir_ / "unused_global.lua").string());
  auto *globals = req.mutable_globals();
  auto *map = globals->mutable_map();

  VariableValue voltage_val;
  voltage_val.set_d(5.0);
  (*map)["voltage"] = voltage_val;

  VariableValue unused;
  unused.set_i(999);
  (*map)["unusedParam"] = unused;
  // This should trigger a warning
  auto *type_manifest = req.mutable_type_manifest();
  auto *parameters = type_manifest->mutable_parameters();
  {
    auto *p = parameters->Add();
    p->set_name("voltage");
    p->set_type(LUA_TYPES_DOUBLE);
  }

  MeasureJobResultResponse resp{};
  int result = handle_measure(req, &resp);

  EXPECT_EQ(result, 0);
  EXPECT_TRUE(resp.standard_response().ok());

  auto log = read_log();
  // be less brittle — look for the variable name and the "not used" phrase
  // separately
  std::cout << "The entire log content:\n" << log << '\n'; // For debugging"
  EXPECT_NE(log.find("unusedParam"), std::string::npos);
  EXPECT_NE(log.find("not used"), std::string::npos);
}

TEST_F(TypeManifestTest, CallStackStdStringIsSafe) {
  CallStack *stack = instrument_call_stack_create("scope1", "A", 3, "MEASURE");
  ASSERT_NE(stack, nullptr);

  char *serialized = instrument_call_stack_serialize(stack);
  ASSERT_NE(serialized, nullptr);

  std::string s(serialized);

  // ✅ No truncation
  EXPECT_EQ(s.size(), strlen(serialized));

  // ✅ Roundtrip works through std::string
  CallStack *copy = instrument_call_stack_deserialize(s.c_str());
  ASSERT_NE(copy, nullptr);

  EXPECT_EQ(std::string(instrument_call_stack_get_instrument_name(copy)),
            "scope1");

  EXPECT_EQ(std::string(instrument_call_stack_get_channel_group(copy)), "A");

  EXPECT_EQ(instrument_call_stack_get_channel(copy), 3);

  EXPECT_EQ(std::string(instrument_call_stack_get_command(copy)), "MEASURE");

  instrument_call_stack_free(stack);
  instrument_call_stack_free(copy);
  free(serialized);
}

TEST_F(TypeManifestTest, CallStackDeserializationSuccess) {
  create_test_script("callstack_ok.lua", R"lua(
    function main(ctx, stack)
      local name = stack:get_instrument_name()
      local group = stack:get_channel_group()
      local channel = stack:get_channel()
      local cmd = stack:get_command()

      ctx:log("Instrument: " .. tostring(name))
      ctx:log("Group: " .. tostring(group))
      ctx:log("Channel: " .. tostring(channel))
      ctx:log("Command: " .. tostring(cmd))

      return nil
    end
  )lua");

  // Create a native CallStack and serialize it
  CallStack *stack = instrument_call_stack_create("scope1", "A", 3, "MEASURE");

  ASSERT_NE(stack, nullptr);

  char *serialized = instrument_call_stack_serialize(stack);
  ASSERT_NE(serialized, nullptr);

  MeasureJobRequest req{};
  req.set_script_path((test_scripts_dir_ / "callstack_ok.lua").string());
  auto *globals = req.mutable_globals();
  auto *map = globals->mutable_map();

  VariableValue call_stack_val;
  call_stack_val.set_s(serialized);
  (*map)["stack"] = call_stack_val;
  auto *type_manifest = req.mutable_type_manifest();
  auto *parameters = type_manifest->mutable_parameters();
  {
    auto *p = parameters->Add();
    p->set_name("stack");
    p->set_type(instserver::daemon::v1::LUA_TYPES_CALL_STACK);
  }

  MeasureJobResultResponse resp{};
  int result = handle_measure(req, &resp);

  EXPECT_EQ(result, 0);
  EXPECT_TRUE(resp.standard_response().ok());

  auto log = read_log();

  EXPECT_NE(log.find("Instrument: scope1"), std::string::npos);
  EXPECT_NE(log.find("Group: A"), std::string::npos);
  EXPECT_NE(log.find("Channel: 3"), std::string::npos);
  EXPECT_NE(log.find("Command: MEASURE"), std::string::npos);

  instrument_call_stack_free(stack);
  free(serialized);
}

TEST_F(TypeManifestTest, CallStackDeserializationFailure) {
  create_test_script("callstack_fail.lua", R"lua(
    function main(ctx, stack)
      return nil
    end
  )lua");

  MeasureJobRequest req{};
  req.set_script_path((test_scripts_dir_ / "callstack_fail.lua").string());
  auto *globals = req.mutable_globals();
  auto *map = globals->mutable_map();

  // Intentionally invalid serialized data
  VariableValue call_stack_val;
  call_stack_val.set_s("INVALID_SERIALIZED_DATA");
  (*map)["stack"] = call_stack_val;
  auto *type_manifest = req.mutable_type_manifest();
  auto *parameters = type_manifest->mutable_parameters();
  {
    auto *p = parameters->Add();
    p->set_name("stack");
    p->set_type(instserver::daemon::v1::LUA_TYPES_CALL_STACK);
  }

  MeasureJobResultResponse resp{};
  int result = handle_measure(req, &resp);

  EXPECT_EQ(result, 1);
  EXPECT_FALSE(resp.standard_response().ok());

  EXPECT_NE(resp.standard_response().error().message().find(
                "CallStack deserialization failed"),
            std::string::npos);
}

TEST_F(TypeManifestTest, CallStackWrongJsonType) {
  create_test_script("callstack_wrong_type.lua", R"lua(
    function main(ctx, stack)
      return nil
    end
  )lua");

  MeasureJobRequest req{};
  req.set_script_path(
      (test_scripts_dir_ / "callstack_wrong_type.lua").string());
  auto *globals = req.mutable_globals();
  auto *map = globals->mutable_map();

  // Not a string → should fail
  VariableValue call_stack_val;
  call_stack_val.set_i(12345);
  (*map)["stack"] = call_stack_val;
  auto *type_manifest = req.mutable_type_manifest();
  auto *parameters = type_manifest->mutable_parameters();
  {
    auto *p = parameters->Add();
    p->set_name("stack");
    p->set_type(instserver::daemon::v1::LUA_TYPES_CALL_STACK);
  }

  MeasureJobResultResponse resp{};
  int result = handle_measure(req, &resp);

  EXPECT_EQ(result, 1);
  EXPECT_FALSE(resp.standard_response().ok());

  EXPECT_NE(
      resp.standard_response().error().message().find("CallStack must be"),
      std::string::npos);
}
