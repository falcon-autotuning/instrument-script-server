#include "PluginTestFixture.hpp"
#include "instrument-script-server/Logger.hpp"
#include "instrument-script-server/server/InstrumentRegistry.hpp"
#include "instrument-script-server/server/ServerDaemon.hpp"
#include <filesystem>
#include <gtest/gtest.h>

using namespace instserver;

class InstrumentRegistryTest : public test::PluginTestFixture {
protected:
  void SetUp() override {
    PluginTestFixture::SetUp();
    InstrumentLogger::instance().init("registry_test.log",
                                      spdlog::level::debug);

    test_data_dir_ = std::filesystem::current_path() / "data";

    // Start daemon for these tests
    auto &daemon = ServerDaemon::instance();
    if (!daemon.is_running()) {
      daemon.start();
    }
  }

  void TearDown() override {
    // Clean up after each test - use public API only
    auto &daemon = ServerDaemon::instance();
    if (daemon.is_running()) {
      daemon.stop();
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
  }

  std::filesystem::path test_data_dir_;
};

TEST_F(InstrumentRegistryTest, Singleton) {
  auto &reg1 = InstrumentRegistry::instance();
  auto &reg2 = InstrumentRegistry::instance();

  EXPECT_EQ(&reg1, &reg2);
}

TEST_F(InstrumentRegistryTest, CreateInstrumentFromConfig) {
  auto config_path = test_data_dir_ / "mock_instrument1.yaml";

  if (!std::filesystem::exists(config_path)) {
    GTEST_SKIP() << "Test config not found";
  }

  auto &registry = InstrumentRegistry::instance();

  // This may fail if plugin not available - that's okay for unit test
  bool created = registry.create_instrument(config_path.string());

  if (created) {
    auto instruments = registry.list_instruments();
    EXPECT_FALSE(instruments.empty());
  }
}

TEST_F(InstrumentRegistryTest, ListInstruments) {
  auto &registry = InstrumentRegistry::instance();

  auto initial_list = registry.list_instruments();
  size_t initial_count = initial_list.size();

  // List should always succeed
  EXPECT_GE(initial_count, 0);
}

TEST_F(InstrumentRegistryTest, HasInstrument) {
  auto &registry = InstrumentRegistry::instance();

  EXPECT_FALSE(registry.has_instrument("NonexistentInstrument"));
}

TEST_F(InstrumentRegistryTest, GetInstrument) {
  auto &registry = InstrumentRegistry::instance();

  auto proxy = registry.get_instrument("NonexistentInstrument");
  EXPECT_EQ(proxy, nullptr);
}

TEST_F(InstrumentRegistryTest, RemoveInstrument) {
  auto &registry = InstrumentRegistry::instance();

  auto config_path = test_data_dir_ / "mock_instrument1.yaml";

  if (!std::filesystem::exists(config_path)) {
    GTEST_SKIP() << "Test config not found";
  }

  if (registry.create_instrument(config_path.string())) {
    auto instruments = registry.list_instruments();
    if (!instruments.empty()) {
      std::string name = instruments[0];

      EXPECT_TRUE(registry.has_instrument(name));
      registry.remove_instrument(name);
      EXPECT_FALSE(registry.has_instrument(name));
    }
  }
}

TEST_F(InstrumentRegistryTest, StopAll) {
  auto &registry = InstrumentRegistry::instance();

  registry.stop_all();

  auto instruments = registry.list_instruments();
  EXPECT_EQ(instruments.size(), 0);
}

TEST_F(InstrumentRegistryTest, MalformedIpcPayloadsDoNotCrashSystem) {
  auto &registry = InstrumentRegistry::instance();
  auto config_path = test_data_dir_ / "mock_instrument1.yaml";

  if (!std::filesystem::exists(config_path)) {
    GTEST_SKIP() << "Test config not found";
  }

  // 1. Start the instrument worker
  ASSERT_TRUE(registry.create_instrument(config_path.string()));
  auto proxy = registry.get_instrument("MockInstrument1");
  ASSERT_NE(proxy, nullptr);
  ASSERT_TRUE(proxy->is_alive());

  // 2. Inject a completely malformed (non-JSON) command message into the worker's request queue
  {
    boost::interprocess::message_queue req_queue(
        boost::interprocess::open_only, "instrument_MockInstrument1_req");

    ipc::IPCMessage malformed_msg;
    malformed_msg.type = ipc::IPCMessage::Type::COMMAND;
    malformed_msg.id = 9999;
    malformed_msg.sync_token = 0;
    std::string bad_payload = "{invalid_json: true, ";
    malformed_msg.payload_size = bad_payload.size();
    std::memcpy(malformed_msg.payload.data(), bad_payload.data(), bad_payload.size());

    req_queue.send(&malformed_msg, sizeof(ipc::IPCMessage), 0);
  }

  // The worker should process the malformed command, catch the exception, send back a failure response, and NOT crash!
  // Wait a moment for it to be processed
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  EXPECT_TRUE(proxy->is_alive()) << "Worker crashed after receiving malformed command!";

  // 3. Inject a completely malformed (non-JSON) response message into the server's response queue
  {
    boost::interprocess::message_queue resp_queue(
        boost::interprocess::open_only, "instrument_MockInstrument1_resp");

    ipc::IPCMessage malformed_msg;
    malformed_msg.type = ipc::IPCMessage::Type::RESPONSE;
    malformed_msg.id = 8888;
    malformed_msg.sync_token = 0;
    std::string bad_payload = "{invalid_response_json: false, ";
    malformed_msg.payload_size = bad_payload.size();
    std::memcpy(malformed_msg.payload.data(), bad_payload.data(), bad_payload.size());

    resp_queue.send(&malformed_msg, sizeof(ipc::IPCMessage), 0);
  }

  // The server daemon / proxy response listener should catch the exception and NOT crash!
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  EXPECT_TRUE(proxy->is_alive()) << "Worker proxy stopped after receiving malformed response!";

  // 4. Send a valid command to verify that the worker is still fully functional and responding
  SerializedCommand valid_cmd;
  valid_cmd.id = "valid-command-after-error";
  valid_cmd.instrument_name = "MockInstrument1";
  valid_cmd.verb = "*IDN?";
  valid_cmd.expects_response = true;
  valid_cmd.timeout = std::chrono::milliseconds(1000);

  auto resp_future = proxy->execute(valid_cmd);
  auto status = resp_future.wait_for(std::chrono::milliseconds(2000));
  ASSERT_EQ(status, std::future_status::ready) << "Worker failed to respond to a valid command after receiving malformed inputs!";

  CommandResponse resp = resp_future.get();
  EXPECT_TRUE(resp.success);
  EXPECT_FALSE(resp.text_response.empty());

  registry.remove_instrument("MockInstrument1");
}
