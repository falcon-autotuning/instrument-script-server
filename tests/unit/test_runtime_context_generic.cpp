#include "instrument-server/Logger.hpp"
#include "instrument-server/server/InstrumentRegistry.hpp"
#include "instrument-server/server/RuntimeContext.hpp"
#include "instrument-server/server/SyncCoordinator.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <sol/sol.hpp>
#include <spdlog/spdlog.h>
#include <thread>

using namespace instserver;

class RuntimeContextGenericTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Ensure registry is clean
    registry_ = &InstrumentRegistry::instance();
    registry_->stop_all();

    sync_coordinator_ = std::make_unique<SyncCoordinator>();

    // Create a dedicated temp log file for each test run (absolute path)
    auto tmp = std::filesystem::temp_directory_path();
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    log_path_ = tmp / ("instrument_test_" + std::to_string(now) + ".log");

    // Ensure any previous instrument logger state is fully cleared so init()
    // will recreate sinks that write to our test file.
    InstrumentLogger::instance().shutdown();

    // Initialize logging to our temp file and set level to debug so tests see
    // messages.
    InstrumentLogger::instance().init(log_path_.string(), spdlog::level::debug);

    // Ensure debug/info messages are flushed promptly
    if (auto l = spdlog::get("instrument")) {
      l->flush_on(spdlog::level::debug);
    }

    // Prepare Lua and bindings. bind_runtime_context is expected to inject
    // `context`
    lua_ = std::make_unique<sol::state>();
    lua_->open_libraries(sol::lib::base, sol::lib::math, sol::lib::string);
    bind_runtime_context(*lua_, *registry_, *sync_coordinator_);
  }

  void TearDown() override {
    // Give logger a brief moment to flush then flush explicitly
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    if (auto l = spdlog::get("instrument")) {
      l->flush();
    }

    // Clean up Lua and instruments
    lua_.reset();
    registry_->stop_all();

    // Shutdown logger to allow clean re-init in other tests
    InstrumentLogger::instance().shutdown();

    // Remove the temporary log file (with non-throwing error_code)
    std::error_code ec;
    std::filesystem::remove(log_path_, ec);
  }

  // Helper to read the current log contents
  std::string read_log() {
    if (auto l = spdlog::get("instrument")) {
      l->flush();
    }
    // Small delay to allow file system to catch up
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    std::ifstream ifs(log_path_, std::ios::in | std::ios::binary);
    if (!ifs)
      return "";
    std::string contents((std::istreambuf_iterator<char>(ifs)),
                         (std::istreambuf_iterator<char>()));
    return contents;
  }

  // Helper to assert the log contains a substring (with optional wait for async
  // flush)
  void expect_log_contains(const std::string &substr, int wait_ms = 5) {
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

// Test: calling a missing instrument should produce specific log entries
TEST_F(RuntimeContextGenericTest, CallFunctionLogsMissingInstrument) {
  lua_->script(R"(
    result = context:call("FakeInstrument.Command")
  )");

  expect_log_contains("Calling function: FakeInstrument.Command");
  expect_log_contains("No metadata found for instrument: FakeInstrument");
  expect_log_contains("Command failed: Instrument not found: FakeInstrument");
}

// Test: empty parallel block should log start and execution of 0 buffered
// commands
TEST_F(RuntimeContextGenericTest, ParallelBlockLogsStartAndEmptyExecution) {
  lua_->script(R"(
    context:parallel(function()
      -- Empty parallel block
    end)
  )");

  expect_log_contains("Starting parallel block");
  expect_log_contains("Executing 0 buffered commands");
}

// Test: log() from Lua should produce a user-level log entry
TEST_F(RuntimeContextGenericTest, LogFunctionEmitsUserMessage) {
  lua_->script(R"(
    context:log("Test log message")
  )");

  expect_log_contains("Test log message");
}

// Test: parsing various instrument command formats should be logged
TEST_F(RuntimeContextGenericTest, ParseInstrumentCommandFormatsAreLogged) {
  lua_->script(R"(
    context:call("Inst1.Command")
    context:call("Inst1:1.Command")
    context:call("Inst1:2.Command", 5.0)
  )");

  expect_log_contains("Calling function: Inst1.Command");
  expect_log_contains("Calling function: Inst1:1.Command");
  expect_log_contains("Calling function: Inst1:2.Command");
  expect_log_contains("No metadata found for instrument: Inst1");
  expect_log_contains("Command failed: Instrument not found: Inst1");
}

// Test: parallel buffering should buffer 3 commands and attempt execution of 3
// commands
TEST_F(RuntimeContextGenericTest, ParallelWithBufferingBuffersCommands) {
  auto ctx = std::make_unique<RuntimeContext>(*registry_, *sync_coordinator_);
  (*lua_)["context"] = ctx.get();

  lua_->script(R"(
    context:parallel(function()
      context:call("Inst1.Command1")
      context:call("Inst2.Command2")
      context:call("Inst3.Command3")
    end)
  )");

  expect_log_contains("Starting parallel block");
  expect_log_contains("Buffered parallel command");
  expect_log_contains("Executing 3 buffered commands");
  expect_log_contains("Instrument not found: Inst1");
  expect_log_contains("Instrument not found: Inst2");
  expect_log_contains("Instrument not found: Inst3");
}

// Test: nested calls and helper functions should log in order
TEST_F(RuntimeContextGenericTest, NestedCallsProduceUserLogsInOrder) {
  lua_->script(R"(
    function helper()
      context:log("Helper function")
    end
    context:log("Main")
    helper()
    context:log("Done")
  )");

  auto contents = read_log();
  auto pos_main = contents.find("Main");
  auto pos_helper = contents.find("Helper function");
  auto pos_done = contents.find("Done");

  EXPECT_NE(pos_main, std::string::npos);
  EXPECT_NE(pos_helper, std::string::npos);
  EXPECT_NE(pos_done, std::string::npos);
  EXPECT_LT(pos_main, pos_helper);
  EXPECT_LT(pos_helper, pos_done);
}
