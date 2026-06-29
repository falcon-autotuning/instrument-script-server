#pragma once
#include "instrument-script-server/daemon/InstrumentRegistry.hpp"
#include "instrument-script-server/daemon/SyncCoordinator.hpp"

#include <gtest/gtest.h>

namespace instserver::test {

class InstrumentServerTest : public ::testing::Test {
protected:
  void SetUp() override;
  void TearDown() override;

  daemon::InstrumentRegistry *registry_;
  SyncCoordinator *sync_coordinator_;
};

class IntegrationTest : public ::testing::Test {
protected:
  void SetUp() override;
  void TearDown() override;

  std::string test_data_dir_;
  std::string mock_plugin_path_;
};

} // namespace instserver::test
