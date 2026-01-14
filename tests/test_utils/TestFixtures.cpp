#include "TestFixtures.hpp"
#include "instrument-server/Logger.hpp"

#include <filesystem>

namespace instserver {
namespace test {

void InstrumentServerTest::SetUp() {
  InstrumentLogger::instance().init("test.log", spdlog::level::debug);
  registry_ = &InstrumentRegistry::instance();
  sync_coordinator_ = new SyncCoordinator();
}

void InstrumentServerTest::TearDown() {
  registry_->stop_all();
  delete sync_coordinator_;
}

void IntegrationTest::SetUp() {
  InstrumentLogger::instance().init("integration_test.log",
                                    spdlog::level::debug);

  // Find test data directory
  test_data_dir_ =
      (std::filesystem::current_path() / "tests" / "data").string();

  // Find mock plugin
  mock_plugin_path_ =
      (std::filesystem::current_path() / "tests" / "mock_plugin.so").string();

  if (!std::filesystem::exists(mock_plugin_path_)) {
    // Try alternate location
    mock_plugin_path_ = "./mock_plugin.so";
  }
}

void IntegrationTest::TearDown() {
  auto &registry = InstrumentRegistry::instance();
  registry.stop_all();
}

} // namespace test
} // namespace instserver
