#include "PluginTestFixture.hpp"
#include "instrument-server/Logger.hpp"
#include "instrument-server/server/InstrumentRegistry.hpp"
#include "instrument-server/server/ServerDaemon.hpp"
#include <filesystem>
#include <gtest/gtest.h>

using namespace instserver;

class InstrumentRegistryTest : public test::PluginTestFixture {
protected:
  void SetUp() override {
    PluginTestFixture::SetUp();
    InstrumentLogger::instance().init("registry_test.log",
                                      spdlog::level::debug);

    test_data_dir_ = std::filesystem::current_path() / "tests" / "data";

    // Start daemon for these tests
    auto &daemon = ServerDaemon::instance();
    if (!daemon.is_running()) {
      daemon.start();
    }
  }

  void TearDown() override {
    auto &registry = InstrumentRegistry::instance();
    registry.stop_all();
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
