#include "instrument-script-server/plugin/PluginRegistry.hpp"
#include "instrument-script-server/server/InstrumentRegistry.hpp"
#include "instrument-script-server/server/RuntimeContext.hpp"
#include "instrument-script-server/server/SyncCoordinator.hpp"
#include <instrument-log/inst_logging.h>

#include <fstream>
#include <gtest/gtest.h>
#include <sol/sol.hpp>
#include <spdlog/spdlog.h>
#include <thread>

using namespace instserver;

class MainFunctionTest : public ::testing::Test {
protected:
  void SetUp() override {
    registry_ = &InstrumentRegistry::instance();
    registry_->stop_all();

    sync_coordinator_ = std::make_unique<SyncCoordinator>();

    auto tmp = std::filesystem::temp_directory_path();
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    log_path_ = tmp / ("instrument_test_" + std::to_string(now) + ".log");

    inst_log_shutdown();
    inst_log_init(log_path_.string().c_str(), INST_LOG_DEBUG, "instrument",
                  1024 * 1024, // 1 MB
                  3);          // rotation count
    lua_ = std::make_unique<sol::state>();
    lua_->open_libraries(sol::lib::base, sol::lib::math, sol::lib::string);
    bind_runtime_context(*lua_, *registry_, *sync_coordinator_);
  }

  void TearDown() override {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    if (auto l = spdlog::get("instrument")) {
      l->flush();
    }

    lua_.reset();
    registry_->stop_all();
    auto &plugin_registry = instserver::plugin::PluginRegistry::instance();
    plugin_registry.unload_all();
    inst_log_flush();
    inst_log_shutdown();

    std::error_code ec;
    std::filesystem::remove(log_path_, ec);
  }

  std::string read_log() {
    if (auto l = spdlog::get("instrument")) {
      l->flush();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    std::ifstream ifs(log_path_, std::ios::in | std::ios::binary);
    if (!ifs)
      return "";
    std::string contents((std::istreambuf_iterator<char>(ifs)),
                         (std::istreambuf_iterator<char>()));
    return contents;
  }

  void expect_log_contains(const std::string &substr, int wait_ms = 10) {
    if (wait_ms > 0)
      std::this_thread::sleep_for(std::chrono::milliseconds(wait_ms));
    auto contents = read_log();
    EXPECT_NE(contents.find(substr), std::string::npos)
        << "Log did not contain: " << substr << "\nFull log:\n"
        << contents;
  }

  InstrumentRegistry *registry_{nullptr};
  std::unique_ptr<SyncCoordinator> sync_coordinator_;
  std::unique_ptr<sol::state> lua_;
  std::filesystem::path log_path_;
};

// Test that main function is called with context parameter
TEST_F(MainFunctionTest, MainFunctionReceivesContext) {
  const char *script = R"lua(
    function main(ctx)
      ctx:log("Main function called with context")
      return nil
    end
  )lua";

  auto result = lua_->safe_script(script);
  ASSERT_TRUE(result.valid());

  sol::optional<sol::function> main_func = (*lua_)["main"];
  ASSERT_TRUE(main_func.has_value());

  auto ctx = std::make_shared<RuntimeContext>(*registry_, *sync_coordinator_);
  sol::protected_function_result call_result = (*main_func)(ctx.get());

  ASSERT_TRUE(call_result.valid());
  expect_log_contains("Main function called with context");
}

// Test that context:error() sets error state
TEST_F(MainFunctionTest, ContextErrorSetsErrorState) {
  const char *script = R"lua(
    function main(ctx)
      ctx:error("Test error message")
      return nil
    end
  )lua";

  auto result = lua_->safe_script(script);
  ASSERT_TRUE(result.valid());

  sol::optional<sol::function> main_func = (*lua_)["main"];
  ASSERT_TRUE(main_func.has_value());

  auto ctx = std::make_shared<RuntimeContext>(*registry_, *sync_coordinator_);
  sol::protected_function_result call_result = (*main_func)(ctx.get());

  ASSERT_TRUE(call_result.valid());
  EXPECT_TRUE(ctx->has_error());
  EXPECT_EQ(ctx->get_error(), "Test error message");
  expect_log_contains("Test error message");
}

// Test that return value from main is optional
TEST_F(MainFunctionTest, MainReturnValueOptional) {
  const char *script_with_return = R"lua(
    function main(ctx)
      ctx:log("Test with return")
      return {result = "success"}
    end
  )lua";

  const char *script_without_return = R"lua(
    function main(ctx)
      ctx:log("Test without return")
      return nil
    end
  )lua";

  // Test with return value
  auto result1 = lua_->safe_script(script_with_return);
  ASSERT_TRUE(result1.valid());

  sol::optional<sol::function> main_func1 = (*lua_)["main"];
  auto ctx1 = std::make_shared<RuntimeContext>(*registry_, *sync_coordinator_);
  sol::protected_function_result call_result1 = (*main_func1)(ctx1.get());
  ASSERT_TRUE(call_result1.valid());

  // Test without return value
  auto result2 = lua_->safe_script(script_without_return);
  ASSERT_TRUE(result2.valid());

  sol::optional<sol::function> main_func2 = (*lua_)["main"];
  auto ctx2 = std::make_shared<RuntimeContext>(*registry_, *sync_coordinator_);
  sol::protected_function_result call_result2 = (*main_func2)(ctx2.get());
  ASSERT_TRUE(call_result2.valid());
}
