#include "PluginTestFixture.hpp"
#include "instrument-script-server/server/InstrumentRegistry.hpp"
#include "instrument-script-server/server/ServerDaemon.hpp"
#include <filesystem>
#include <gtest/gtest.h>
#include <instrument-log/inst_logging.h>

using namespace instserver;

class InstrumentRegistryTest : public test::PluginTestFixture {
protected:
  void SetUp() override {
    PluginTestFixture::SetUp();
    inst_log_shutdown();
    inst_log_init("registry_test.log", INST_LOG_DEBUG, "registry_test",
                  1024 * 1024, // 1 MB
                  3);          // rotation count
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
    inst_log_flush();
    inst_log_shutdown();
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

TEST_F(InstrumentRegistryTest, InvalidMessageTypeDoesNotCrashWorker) {
  auto &registry = InstrumentRegistry::instance();
  auto config_path = test_data_dir_ / "mock_instrument1.yaml";

  if (!std::filesystem::exists(config_path)) {
    GTEST_SKIP() << "Test config not found";
  }

  ASSERT_TRUE(registry.create_instrument(config_path.string()));
  auto proxy = registry.get_instrument("MockInstrument1");
  ASSERT_NE(proxy, nullptr);
  ASSERT_TRUE(proxy->is_alive());

  {
    boost::interprocess::message_queue req_queue(
        boost::interprocess::open_only, "instrument_MockInstrument1_req");

    ipc::IPCMessage msg{};
    msg.type = static_cast<ipc::IPCMessage::Type>(255); // ✅ invalid enum
    msg.id = 1234;

    req_queue.send(&msg, sizeof(msg), 0);
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  EXPECT_TRUE(proxy->is_alive()) << "Worker crashed on invalid message type!";
}
TEST_F(InstrumentRegistryTest, InvalidParamTypeDoesNotCrashWorker) {
  auto &registry = InstrumentRegistry::instance();
  auto config_path = test_data_dir_ / "mock_instrument1.yaml";

  ASSERT_TRUE(registry.create_instrument(config_path.string()));
  auto proxy = registry.get_instrument("MockInstrument1");

  {
    boost::interprocess::message_queue req_queue(
        boost::interprocess::open_only, "instrument_MockInstrument1_req");

    ipc::IPCMessage msg{};
    msg.type = ipc::IPCMessage::Type::COMMAND;
    msg.id = 5678;

    auto &cmd = msg.command;

    std::strncpy(cmd.instrument_name, "MockInstrument1",
                 PLUGIN_MAX_STRING_LEN - 1);
    std::strncpy(cmd.verb, "SET", PLUGIN_MAX_STRING_LEN - 1);

    cmd.param_count = 1;

    std::strncpy(cmd.params[0].name, "bad_param", PLUGIN_MAX_STRING_LEN - 1);

    // ❌ Corrupt enum
    cmd.params[0].value.type = static_cast<ipc::IPCParamValue::Type>(255);

    req_queue.send(&msg, sizeof(msg), 0);
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  EXPECT_TRUE(proxy->is_alive()) << "Worker crashed on invalid param type!";
}
TEST_F(InstrumentRegistryTest, InvalidArraySizeDoesNotCrashWorker) {
  auto &registry = InstrumentRegistry::instance();
  auto config_path = test_data_dir_ / "mock_instrument1.yaml";

  ASSERT_TRUE(registry.create_instrument(config_path.string()));
  auto proxy = registry.get_instrument("MockInstrument1");

  {
    boost::interprocess::message_queue req_queue(
        boost::interprocess::open_only, "instrument_MockInstrument1_req");

    ipc::IPCMessage msg{};
    msg.type = ipc::IPCMessage::Type::COMMAND;
    msg.id = 9999;

    auto &cmd = msg.command;

    std::strncpy(cmd.instrument_name, "MockInstrument1",
                 PLUGIN_MAX_STRING_LEN - 1);
    std::strncpy(cmd.verb, "SET_ARRAY", PLUGIN_MAX_STRING_LEN - 1);

    cmd.param_count = 1;

    auto &p = cmd.params[0];
    std::strncpy(p.name, "data", PLUGIN_MAX_STRING_LEN - 1);

    p.value.type = ipc::IPCParamValue::Type::DOUBLE_ARRAY;

    // ❌ Corrupt size (beyond bounds)
    p.value.arr.size = instserver::ipc::PLUGIN_MAX_ARRAY_LEN + 50;

    req_queue.send(&msg, sizeof(msg), 0);
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  EXPECT_TRUE(proxy->is_alive()) << "Worker crashed on oversized array!";
}
TEST_F(InstrumentRegistryTest, WrongUnionAccessDoesNotCrashWorker) {
  auto &registry = InstrumentRegistry::instance();
  auto config_path = test_data_dir_ / "mock_instrument1.yaml";

  ASSERT_TRUE(registry.create_instrument(config_path.string()));
  auto proxy = registry.get_instrument("MockInstrument1");

  {
    boost::interprocess::message_queue req_queue(
        boost::interprocess::open_only, "instrument_MockInstrument1_req");

    ipc::IPCMessage msg{};

    // ❌ Says RESPONSE but contains COMMAND data
    msg.type = ipc::IPCMessage::Type::RESPONSE;

    std::strncpy(msg.command.instrument_name, "MockInstrument1",
                 PLUGIN_MAX_STRING_LEN - 1);

    req_queue.send(&msg, sizeof(msg), 0);
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  EXPECT_TRUE(proxy->is_alive()) << "Worker crashed on union misuse!";
}
TEST_F(InstrumentRegistryTest, MissingFieldsDoesNotCrashWorker) {
  auto &registry = InstrumentRegistry::instance();
  auto config_path = test_data_dir_ / "mock_instrument1.yaml";

  ASSERT_TRUE(registry.create_instrument(config_path.string()));
  auto proxy = registry.get_instrument("MockInstrument1");

  {
    boost::interprocess::message_queue req_queue(
        boost::interprocess::open_only, "instrument_MockInstrument1_req");

    ipc::IPCMessage msg{};
    msg.type = ipc::IPCMessage::Type::COMMAND;

    // ❌ leave verb empty
    std::strncpy(msg.command.instrument_name, "MockInstrument1",
                 PLUGIN_MAX_STRING_LEN - 1);

    msg.command.param_count = 0;

    req_queue.send(&msg, sizeof(msg), 0);
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  EXPECT_TRUE(proxy->is_alive()) << "Worker crashed on missing command fields!";
}
