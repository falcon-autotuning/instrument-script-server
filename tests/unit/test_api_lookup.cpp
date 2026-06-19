#include "PluginTestFixture.hpp"
#include "instrument-script-server/server/InstrumentRegistry.hpp"
#include <instrument-log/inst_logging.h>

#include <filesystem>
#include <gtest/gtest.h>
#include <instrument-plugin.h>
#include <spdlog/common.h>

using namespace instserver;

class APILookupTest : public test::PluginTestFixture {
protected:
  void SetUp() override {
    // Ensure clean state between tests
    inst_log_shutdown();
    auto tmp = std::filesystem::temp_directory_path();
    auto log_path = tmp / "instrument_test.log";
    inst_log_init(log_path.string().c_str(), INST_LOG_DEBUG, "instrument_test",
                  1024 * 1024, // 1 MB
                  3);          // rotation count
    PluginTestFixture::SetUp();
    registry_ = &InstrumentRegistry::instance();
    test_data_dir_ = std::filesystem::current_path() / "data";
    config_path_ = test_data_dir_ / "mock_instrument1.yaml";
    if (!std::filesystem::exists(config_path_)) {
      GTEST_SKIP() << "Config not found at: " << config_path_;
    }
  }

  void TearDown() override {
    registry_->stop_all();
    inst_log_flush();
    inst_log_shutdown();
  }

  InstrumentRegistry *registry_;
  std::filesystem::path test_data_dir_;
  std::filesystem::path config_path_;
};

TEST_F(APILookupTest, CommandExpectsResponse) {
  ASSERT_TRUE(registry_->create_instrument(config_path_.string()));

  // Commands with non-empty outputs array expect response
  EXPECT_TRUE(registry_->get_instrument("MockInstrument1")
                  ->command_expects_response("IDN"));
  EXPECT_TRUE(registry_->get_instrument("MockInstrument1")
                  ->command_expects_response("ECHO"));
  EXPECT_TRUE(registry_->get_instrument("MockInstrument1")
                  ->command_expects_response("MEASURE"));
  EXPECT_TRUE(registry_->get_instrument("MockInstrument1")
                  ->command_expects_response("GET"));
  EXPECT_TRUE(registry_->get_instrument("MockInstrument1")
                  ->command_expects_response("GET_STRING"));
  EXPECT_TRUE(registry_->get_instrument("MockInstrument1")
                  ->command_expects_response("GET_BOOL"));
  EXPECT_TRUE(registry_->get_instrument("MockInstrument1")
                  ->command_expects_response("GET_ARRAY"));
  EXPECT_TRUE(registry_->get_instrument("MockInstrument1")
                  ->command_expects_response("GET_RANGE"));

  // Commands with empty outputs array don't expect response
  EXPECT_FALSE(registry_->get_instrument("MockInstrument1")
                   ->command_expects_response("SET"));
  EXPECT_FALSE(registry_->get_instrument("MockInstrument1")
                   ->command_expects_response("SET_RANGE"));
  EXPECT_FALSE(registry_->get_instrument("MockInstrument1")
                   ->command_expects_response("CONFIGURE"));
  EXPECT_FALSE(registry_->get_instrument("MockInstrument1")
                   ->command_expects_response("RESET"));
}

TEST_F(APILookupTest, GetResponseType) {
  ASSERT_TRUE(registry_->create_instrument(config_path_.string()));

  // Check response types from io definitions
  auto type_measure =
      registry_->get_instrument("MockInstrument1")->get_responses("MEASURE");
  ASSERT_TRUE(!type_measure.empty());
  EXPECT_EQ(type_measure[0].type, PARAM_TYPE_DOUBLE);

  auto type_idn =
      registry_->get_instrument("MockInstrument1")->get_responses("IDN");
  ASSERT_TRUE(!type_idn.empty());
  EXPECT_EQ(type_idn[0].type, PARAM_TYPE_STRING);

  auto type_bool =
      registry_->get_instrument("MockInstrument1")->get_responses("GET_BOOL");
  ASSERT_TRUE(!type_bool.empty());
  EXPECT_EQ(type_bool[0].type, PARAM_TYPE_BOOL);

  auto type_array =
      registry_->get_instrument("MockInstrument1")->get_responses("GET_ARRAY");
  ASSERT_TRUE(!type_array.empty());
  EXPECT_EQ(type_array[0].type, PARAM_TYPE_BUFFER);

  auto type_range =
      registry_->get_instrument("MockInstrument1")->get_responses("GET_RANGE");
  ASSERT_TRUE(!type_range.empty());
  EXPECT_EQ(type_range[0].type, PARAM_TYPE_DOUBLE);

  // Commands without outputs should return nullopt
  auto type_set =
      registry_->get_instrument("MockInstrument1")->get_responses("SET");
  EXPECT_TRUE(type_set.empty());

  auto type_reset =
      registry_->get_instrument("MockInstrument1")->get_responses("RESET");
  EXPECT_TRUE(type_reset.empty());
}

TEST_F(APILookupTest, UnknownCommand) {
  ASSERT_TRUE(registry_->create_instrument(config_path_.string()));

  // Unknown command should default to no response expected
  EXPECT_FALSE(registry_->get_instrument("MockInstrument1")
                   ->command_expects_response("NONEXISTENT"));

  auto type = registry_->get_instrument("MockInstrument1")
                  ->get_responses("NONEXISTENT");
  EXPECT_TRUE(type.empty());
}
