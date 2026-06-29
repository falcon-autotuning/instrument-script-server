#include "TestFixtures.hpp"
#include <instrument-log/inst_logging.h>

#include <filesystem>

namespace instserver::test {

void InstrumentServerTest::SetUp() {
  inst_log_shutdown();
  inst_log_init("test.log", INST_LOG_DEBUG, "instrument",
                1024 * 1024, // 1 MB
                3);          // rotation count
  registry_ = &daemon::InstrumentRegistry::instance();
  sync_coordinator_ = new SyncCoordinator();
}

void InstrumentServerTest::TearDown() {
  registry_->stop_all();
  delete sync_coordinator_;
  inst_log_flush();
  inst_log_shutdown();
}

void IntegrationTest::SetUp() {
  inst_log_shutdown();
  inst_log_init("integrationtest.log", INST_LOG_DEBUG, "instrument",
                1024 * 1024, // 1 MB
                3);          // rotation count

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
  auto &registry = daemon::InstrumentRegistry::instance();
  registry.stop_all();
  inst_log_flush();
  inst_log_shutdown();
}

} // namespace instserver::test
