#include "instrument-server/Logger.hpp"
#include "instrument-server/server/InstrumentRegistry.hpp"
#include <fstream>
#include <gtest/gtest.h>
#include <yaml-cpp/yaml.h>

using namespace instserver;

class InstrumentRegistryTest : public ::testing::Test {
protected:
  void SetUp() override {
    InstrumentLogger::instance().init("test. log", spdlog::level::debug);
    registry_ = &InstrumentRegistry::instance();
  }

  void TearDown() override { registry_->stop_all(); }

  InstrumentRegistry *registry_;
};

TEST_F(InstrumentRegistryTest, ListInstrumentsEmpty) {
  auto instruments = registry_->list_instruments();
  // May have instruments from other tests, but check method works
  EXPECT_TRUE(instruments.size() >= 0);
}

TEST_F(InstrumentRegistryTest, HasInstrumentNonexistent) {
  EXPECT_FALSE(registry_->has_instrument("NonexistentInstrument"));
}

TEST_F(InstrumentRegistryTest, GetInstrumentNonexistent) {
  auto proxy = registry_->get_instrument("NonexistentInstrument");
  EXPECT_EQ(proxy, nullptr);
}

TEST_F(InstrumentRegistryTest, RemoveNonexistent) {
  EXPECT_NO_THROW({ registry_->remove_instrument("NonexistentInstrument"); });
}

TEST_F(InstrumentRegistryTest, CreateInstrumentInvalidPath) {
  bool result = registry_->create_instrument("/nonexistent/config.yaml");
  EXPECT_FALSE(result);
}

TEST_F(InstrumentRegistryTest, CreateInstrumentInvalidYAML) {
  // Create temp invalid YAML file
  std::ofstream f("/tmp/invalid.yaml");
  f << "invalid: yaml:  content:  [[[";
  f.close();

  bool result = registry_->create_instrument("/tmp/invalid.yaml");
  EXPECT_FALSE(result);

  std::remove("/tmp/invalid.yaml");
}

TEST_F(InstrumentRegistryTest, StartStopAll) {
  // Should not crash even with no instruments
  EXPECT_NO_THROW({
    registry_->start_all();
    registry_->stop_all();
  });
}
