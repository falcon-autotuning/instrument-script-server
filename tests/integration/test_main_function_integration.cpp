#include "PluginTestFixture.hpp"
#include "instrument-script-server/plugin/PluginRegistry.hpp"
#include "instrument-script-server/server/CommandHandlers.hpp"
#include "instrument-script-server/server/InstrumentRegistry.hpp"
#include "instrument-script-server/server/ServerDaemon.hpp"
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <instrument-log/inst_logging.h>
#include <nlohmann/json.hpp>
#include <sol/sol.hpp>

#include <google/protobuf/util/json_util.h>

using namespace instserver;
using namespace instserver::test;
using namespace instserver::server;
using json = nlohmann::json;
namespace v1 = instserver::server::v1;

namespace {
int handle_measure(const json &params, json &out) {
  v1::MeasureJobRequest req;

  if (params.contains("script_path")) {
    req.set_script_path(params["script_path"].get<std::string>());
  }

  if (params.contains("globals") && params["globals"].is_object()) {
    auto *globals_map = req.mutable_globals()->mutable_map();
    for (auto it = params["globals"].begin(); it != params["globals"].end();
         ++it) {
      v1::VariableValue val;
      if (it.value().is_number_integer()) {
        val.set_i(it.value().get<int64_t>());
      } else if (it.value().is_number_float()) {
        val.set_d(it.value().get<double>());
      } else if (it.value().is_boolean()) {
        val.set_b(it.value().get<bool>());
      } else if (it.value().is_string()) {
        val.set_s(it.value().get<std::string>());
      } else if (it.value().is_null()) {
        val.set_is_nil(true);
      }
      (*globals_map)[it.key()] = val;
    }
  }

  if (params.contains("type_manifest") && params["type_manifest"].is_object()) {
    auto &tm = params["type_manifest"];
    if (tm.contains("parameters") && tm["parameters"].is_array()) {
      auto *manifest = req.mutable_type_manifest();
      for (const auto &p : tm["parameters"]) {
        auto *param = manifest->add_parameters();
        if (p.contains("name")) {
          param->set_name(p["name"].get<std::string>());
        }
        if (p.contains("type")) {
          std::string type_str = p["type"].get<std::string>();
          v1::LuaTypes ltype = v1::LUA_TYPES_UNSPECIFIED;
          if (type_str == "int")
            ltype = v1::LUA_TYPES_INT64;
          else if (type_str == "number")
            ltype = v1::LUA_TYPES_DOUBLE;
          else if (type_str == "boolean")
            ltype = v1::LUA_TYPES_BOOL;
          else if (type_str == "string")
            ltype = v1::LUA_TYPES_STRING;
          else if (type_str == "DataBuffer")
            ltype = v1::LUA_TYPES_DATA_BUFFER;
          else if (type_str == "CallStack")
            ltype = v1::LUA_TYPES_CALL_STACK;
          param->set_type(ltype);
        }
      }
    }
  }

  v1::MeasureJobResultResponse resp;
  int rc = server::handle_measure(req, &resp);

  std::string json_str;
  google::protobuf::util::JsonPrintOptions options;
  options.preserve_proto_field_names = true;
  auto status =
      google::protobuf::util::MessageToJsonString(resp, &json_str, options);
  if (!status.ok()) {
    return 1;
  }

  out = json::parse(json_str);
  out["ok"] = resp.standard_response().ok();
  if (resp.standard_response().has_error() &&
      !resp.standard_response().error().message().empty()) {
    out["error"] = resp.standard_response().error().message();
  }
  return rc;
}
} // namespace

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
    inst_log_shutdown();
    inst_log_init(log_path_.string().c_str(), INST_LOG_DEBUG, "instrument",
                  1024 * 1024 * 10, // 10 MB
                  3);               // rotation count

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
    auto &plugin_registry = instserver::plugin::PluginRegistry::instance();
    plugin_registry.unload_all();

    // Clean up
    inst_log_flush();
    inst_log_shutdown();
    std::error_code ec;
    std::filesystem::remove_all(test_scripts_dir_, ec);
    std::filesystem::remove(log_path_, ec);
    // Clean up after each test - use public API only
    auto &daemon = ServerDaemon::instance();
    if (daemon.is_running()) {
      daemon.stop();
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
  }

  std::string read_log() {
    inst_log_flush();
    // Increased wait time for slower systems
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    std::ifstream ifs(log_path_, std::ios::in | std::ios::binary);
    if (!ifs) {
      return "";
    }
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
