#include "instrument_script/InstrumentRegistry.hpp"
#include "instrument_script/InstrumentWorkerProxy.hpp"
#include <fstream>
#include <gtest/gtest.h>

using namespace instrument_script;

// This test requires the simple_serial plugin to be built
TEST(WorkerIntegrationTest, DISABLED_EndToEndSerial) {
  // Create minimal config
  nlohmann::json config = {{"name", "TestSerial"},
                           {"connection",
                            {{"type", "SimpleSerial"},
                             {"device", "/dev/null"}, // Dummy device
                             {"plugin", "./simple_serial_plugin.so"}}}};

  nlohmann::json api_def = {{"protocol", {{"type", "SimpleSerial"}}},
                            {"commands", {}}};

  // Create worker proxy
  InstrumentWorkerProxy proxy("TestSerial", "./simple_serial_plugin.so", config,
                              api_def);

  EXPECT_TRUE(proxy.start());
  EXPECT_TRUE(proxy.is_alive());

  // Execute dummy command
  SerializedCommand cmd;
  cmd.id = "test-1";
  cmd.instrument_name = "TestSerial";
  cmd.verb = "READ";
  cmd.expects_response = false;

  auto resp =
      proxy.execute_sync(std::move(cmd), std::chrono::milliseconds(2000));

  // May fail due to /dev/null, but should not crash
  EXPECT_FALSE(resp.command_id.empty());

  proxy.stop();
  EXPECT_FALSE(proxy.is_alive());
}
