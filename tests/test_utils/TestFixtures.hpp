#pragma once
#include "instrument-server/server/InstrumentRegistry.hpp"
#include "instrument-server/server/SyncCoordinator.hpp"

#include <gtest/gtest.h>

namespace instserver {
namespace test {

class InstrumentServerTest : public ::testing::Test {
protected:
  void SetUp() override;
  void TearDown() override;

  InstrumentRegistry *registry_;
  SyncCoordinator *sync_coordinator_;
};

class IntegrationTest : public ::testing::Test {
protected:
  void SetUp() override;
  void TearDown() override;

  std::string test_data_dir_;
  std::string mock_plugin_path_;
};

} // namespace test
} // namespace instserver
