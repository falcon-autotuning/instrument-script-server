#include "instrument-server/Logger.hpp"
#include "instrument-server/server/InstrumentRegistry.hpp"
#include "instrument-server/server/RuntimeContext.hpp"

#include <fstream>
#include <gtest/gtest.h>
#include <sol/sol.hpp>
#include <thread>

using namespace instserver;

// Mock VISA plugin for testing
class MockVISAIntegrationTest : public ::testing::Test {
protected:
  void SetUp() override {
    InstrumentLogger::instance().init("integration_test.log",
                                      spdlog::level::trace);

    // Plugin is built by CMake, just verify it exists
    std::ifstream f("/tmp/mock_visa_plugin.so");
    if (!f.good()) {
      GTEST_SKIP() << "Mock plugin not found.  Build may have failed.";
    }

    // Create mock instrument API
    CreateMockAPI();

    // Create mock instrument config
    CreateMockConfig();
  }

  void TearDown() override {
    std::remove("/tmp/mock_api.yaml");
    std::remove("/tmp/mock_config.yaml");
    auto &registry = InstrumentRegistry::instance();
    registry.stop_all();
  }

  void CreateMockAPI() {
    std::ofstream f("/tmp/mock_api.yaml");
    f << R"(
api_version: "1.0.0"
instrument: 
  vendor: "MockCorp"
  model: "MockDMM"
  identifier: "MOCK1"
  description: "Mock Digital Multimeter for Testing"
protocol:
  type: "MockVISA"
io:
  - name: voltage
    type: float
    role: input
    description: "Voltage to set"
    unit: "V"
  - name: measured_voltage
    type: float
    role: output
    description:  "Measured voltage"
    unit: "V"
commands:
  SET_VOLTAGE:
    description: "Set output voltage"
    parameters:
      - name: voltage
        type:  float
        description: "Voltage in volts"
        unit: "V"
    returns:  void
  MEASURE_VOLTAGE: 
    description: "Measure voltage"
    parameters:  []
    returns: float
  GET_MEASUREMENT_COUNT:
    description: "Get number of measurements"
    parameters: []
    returns: int
  RESET:
    description:  "Reset instrument"
    parameters: []
    returns: void
)";
    f.close();
  }

  void CreateMockConfig() {
    std::ofstream f("/tmp/mock_config.yaml");
    f << R"(
name: MockDMM1
api_ref: /tmp/mock_api.yaml
connection:
  type: MockVISA
  address: "mock://localhost"
  plugin: "/tmp/mock_visa_plugin.so"
io_config:
  voltage: 
    type: float
    role: input
    unit: V
  measured_voltage:
    type: float
    role: output
    unit: V
)";
    f.close();
  }
};

TEST_F(MockVISAIntegrationTest, CreateInstrument) {
  auto &registry = InstrumentRegistry::instance();

  bool created = registry.create_instrument("/tmp/mock_config.yaml");
  ASSERT_TRUE(created) << "Failed to create mock instrument";

  EXPECT_TRUE(registry.has_instrument("MockDMM1"));

  auto proxy = registry.get_instrument("MockDMM1");
  ASSERT_NE(proxy, nullptr);

  // Give worker time to start
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  EXPECT_TRUE(proxy->is_alive());
}

TEST_F(MockVISAIntegrationTest, SetAndMeasureVoltage) {
  auto &registry = InstrumentRegistry::instance();
  ASSERT_TRUE(registry.create_instrument("/tmp/mock_config.yaml"));

  auto proxy = registry.get_instrument("MockDMM1");
  ASSERT_NE(proxy, nullptr);

  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  // Set voltage to 3.3V
  SerializedCommand set_cmd;
  set_cmd.id = "test-set-1";
  set_cmd.instrument_name = "MockDMM1";
  set_cmd.verb = "SET_VOLTAGE";
  set_cmd.params["voltage"] = 3.3;
  set_cmd.expects_response = false;

  auto set_resp =
      proxy->execute_sync(std::move(set_cmd), std::chrono::milliseconds(2000));
  ASSERT_TRUE(set_resp.success)
      << "SET_VOLTAGE failed:  " << set_resp.error_message;

  // Measure voltage
  SerializedCommand meas_cmd;
  meas_cmd.id = "test-meas-1";
  meas_cmd.instrument_name = "MockDMM1";
  meas_cmd.verb = "MEASURE_VOLTAGE";
  meas_cmd.expects_response = true;

  auto meas_resp =
      proxy->execute_sync(std::move(meas_cmd), std::chrono::milliseconds(2000));
  ASSERT_TRUE(meas_resp.success)
      << "MEASURE_VOLTAGE failed: " << meas_resp.error_message;
  ASSERT_TRUE(meas_resp.return_value.has_value());

  double measured = std::get<double>(*meas_resp.return_value);
  EXPECT_NEAR(measured, 3.3, 0.01); // Within 10mV
}

TEST_F(MockVISAIntegrationTest, MultipleMeasurements) {
  auto &registry = InstrumentRegistry::instance();
  ASSERT_TRUE(registry.create_instrument("/tmp/mock_config.yaml"));

  auto proxy = registry.get_instrument("MockDMM1");
  ASSERT_NE(proxy, nullptr);

  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  // Set voltage
  SerializedCommand set_cmd;
  set_cmd.instrument_name = "MockDMM1";
  set_cmd.verb = "SET_VOLTAGE";
  set_cmd.params["voltage"] = 5.0;
  set_cmd.expects_response = false;

  auto set_resp =
      proxy->execute_sync(std::move(set_cmd), std::chrono::milliseconds(2000));
  ASSERT_TRUE(set_resp.success);

  // Perform 10 measurements
  for (int i = 0; i < 10; i++) {
    SerializedCommand meas_cmd;
    meas_cmd.instrument_name = "MockDMM1";
    meas_cmd.verb = "MEASURE_VOLTAGE";
    meas_cmd.expects_response = true;

    auto meas_resp = proxy->execute_sync(std::move(meas_cmd),
                                         std::chrono::milliseconds(2000));
    ASSERT_TRUE(meas_resp.success) << "Measurement " << i << " failed";

    double measured = std::get<double>(*meas_resp.return_value);
    EXPECT_NEAR(measured, 5.0, 0.01);
  }

  // Check measurement count
  SerializedCommand count_cmd;
  count_cmd.instrument_name = "MockDMM1";
  count_cmd.verb = "GET_MEASUREMENT_COUNT";
  count_cmd.expects_response = true;

  auto count_resp = proxy->execute_sync(std::move(count_cmd),
                                        std::chrono::milliseconds(2000));
  ASSERT_TRUE(count_resp.success);

  int count = std::get<int64_t>(*count_resp.return_value);
  EXPECT_EQ(count, 10);
}

TEST_F(MockVISAIntegrationTest, AsyncMeasurements) {
  auto &registry = InstrumentRegistry::instance();
  ASSERT_TRUE(registry.create_instrument("/tmp/mock_config.yaml"));

  auto proxy = registry.get_instrument("MockDMM1");
  ASSERT_NE(proxy, nullptr);

  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  // Enqueue multiple commands asynchronously
  std::vector<std::future<CommandResponse>> futures;

  for (int i = 0; i < 5; i++) {
    SerializedCommand cmd;
    cmd.instrument_name = "MockDMM1";
    cmd.verb = "MEASURE_VOLTAGE";
    cmd.expects_response = true;

    futures.push_back(proxy->execute(std::move(cmd)));
  }

  // Wait for all results
  for (auto &fut : futures) {
    auto resp = fut.get();
    EXPECT_TRUE(resp.success);
  }
}

TEST_F(MockVISAIntegrationTest, LuaScriptIntegration) {
  auto &registry = InstrumentRegistry::instance();
  ASSERT_TRUE(registry.create_instrument("/tmp/mock_config.yaml"));

  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  // Create Lua environment
  sol::state lua;
  lua.open_libraries(sol::lib::base, sol::lib::math);

  // Bind runtime context
  bind_runtime_contexts(lua);

  RuntimeContext_DCGetSet ctx(registry);
  lua["context"] = &ctx;

  // Run Lua script
  std::string script = R"(
        context.log("Starting test measurement")
        
        -- Set voltage
        context.call("MockDMM1.SET_VOLTAGE", {voltage = 2.5})
        
        -- Measure
        local result = context.call("MockDMM1.MEASURE_VOLTAGE", {})
        context.log("Measured: " .. tostring(result))
        
        -- Verify (approximately)
        assert(result > 2.4 and result < 2.6, "Measurement out of range")
        
        context.log("Test passed!")
        return result
    )";

  auto result = lua.safe_script(script);
  EXPECT_TRUE(result.valid()) << "Lua script failed";

  if (result.valid()) {
    double measured = result;
    EXPECT_NEAR(measured, 2.5, 0.1);
  }
}

TEST_F(MockVISAIntegrationTest, ErrorHandling) {
  auto &registry = InstrumentRegistry::instance();
  ASSERT_TRUE(registry.create_instrument("/tmp/mock_config.yaml"));

  auto proxy = registry.get_instrument("MockDMM1");
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  // Send unknown command
  SerializedCommand cmd;
  cmd.instrument_name = "MockDMM1";
  cmd.verb = "UNKNOWN_COMMAND";
  cmd.expects_response = true;

  auto resp =
      proxy->execute_sync(std::move(cmd), std::chrono::milliseconds(2000));
  EXPECT_FALSE(resp.success);
  EXPECT_NE(resp.error_code, 0);
  EXPECT_FALSE(resp.error_message.empty());
}

TEST_F(MockVISAIntegrationTest, Statistics) {
  auto &registry = InstrumentRegistry::instance();
  ASSERT_TRUE(registry.create_instrument("/tmp/mock_config.yaml"));

  auto proxy = registry.get_instrument("MockDMM1");
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  auto stats_before = proxy->get_stats();

  // Execute some commands
  for (int i = 0; i < 5; i++) {
    SerializedCommand cmd;
    cmd.instrument_name = "MockDMM1";
    cmd.verb = "MEASURE_VOLTAGE";
    cmd.expects_response = true;

    proxy->execute_sync(std::move(cmd), std::chrono::milliseconds(2000));
  }

  auto stats_after = proxy->get_stats();

  EXPECT_GT(stats_after.commands_sent, stats_before.commands_sent);
  EXPECT_GT(stats_after.commands_completed, stats_before.commands_completed);
}
