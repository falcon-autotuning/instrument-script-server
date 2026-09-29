
#include "PluginTestFixture.hpp"
#include "instrument-script-server/daemon/CommandHandlers.hpp"
#include "instrument-script-server/daemon/InstrumentRegistry.hpp"
#include "instrument-script-server/daemon/PluginRegistry.hpp"
#include "instrument-script-server/daemon/ServerDaemon.hpp"
#include <chrono>
#include <gtest/gtest.h>
#include <instrument-call-stack/instrument-call-stack-lua.h>
#include <instrument-domain/instrument-domain.h>
#include <instrument-log/inst_logging.h>
#include <instrument-target/instrument-target.h>
#ifndef TEST_DATA_DIR
#define TEST_DATA_DIR "."
#endif

using namespace instserver;
class HandleMeasureTest : public test::PluginTestFixture {
protected:
  void StartMockInstrument() {
    daemon::StartInstrumentRequest req;
    daemon::StartInstrumentResponse resp;

    test_configs_dir_ = std::filesystem::path(TEST_DATA_DIR);

    req.set_config_path(
        (test_configs_dir_ / "mock_instrument_multi1.yaml").string());
    req.set_log_level("debug");

    ASSERT_EQ(daemon::handle_start_instrument(req, &resp), 0);
    ASSERT_TRUE(resp.standard_response().ok());
  }
  void SetUp() override {
    PluginTestFixture::SetUp();

    registry_ = &daemon::InstrumentRegistry::instance();
    registry_->stop_all();

    auto tmp = std::filesystem::temp_directory_path();
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();

    log_path_ = tmp / ("measure_test_" + std::to_string(now) + ".log");

    inst_log_shutdown();
    inst_log_init(log_path_.string().c_str(), INST_LOG_DEBUG, "instrument",
                  1024 * 1024, 3);

    auto &daemon = ServerDaemon::instance();
    daemon.start();
  }

  void TearDown() override {
    registry_->stop_all();
    plugin::PluginRegistry::instance().unload_all();

    inst_log_flush();
    inst_log_shutdown();

    std::error_code ec;
    std::filesystem::remove(log_path_, ec);

    auto &daemon = ServerDaemon::instance();
    daemon.stop();
  }

  std::filesystem::path write_script(const std::string &contents) {
    auto path =
        std::filesystem::temp_directory_path() /
        ("script_" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()) +
         ".lua");

    std::ofstream(path) << contents;
    return path;
  }

  daemon::InstrumentRegistry *registry_{nullptr};
  std::filesystem::path log_path_;
  std::filesystem::path test_configs_dir_;
};

TEST_F(HandleMeasureTest, MissingScriptPathReturnsInvalidArgument) {
  daemon::MeasureJobRequest req;
  daemon::MeasureJobResultResponse resp;

  EXPECT_EQ(daemon::handle_measure(req, &resp), 1);

  EXPECT_FALSE(resp.standard_response().ok());
  EXPECT_EQ(resp.status(), daemon::JOB_STATUS_FAILED);

  EXPECT_EQ(resp.standard_response().error().code(),
            daemon::ERROR_CODE_INVALID_ARGUMENT);

  EXPECT_EQ(resp.standard_response().error().message(), "Missing script_path");
}

TEST_F(HandleMeasureTest, NoInstrumentsRunningReturnsRuntimeError) {
  auto script = write_script(R"(
        function main(ctx)
        end
      )");

  daemon::MeasureJobRequest req;
  req.set_script_path(script.string());

  daemon::MeasureJobResultResponse resp;

  EXPECT_EQ(daemon::handle_measure(req, &resp), 1);

  EXPECT_FALSE(resp.standard_response().ok());

  EXPECT_EQ(resp.standard_response().error().message(),
            "No instruments are running");

  EXPECT_EQ(resp.status(), daemon::JOB_STATUS_FAILED);
}

TEST_F(HandleMeasureTest, MissingMainFunctionFails) {
  StartMockInstrument();

  auto script = write_script(R"(
    local x = 42
  )");

  daemon::MeasureJobRequest req;
  req.set_script_path(script.string());

  daemon::MeasureJobResultResponse resp;

  EXPECT_EQ(daemon::handle_measure(req, &resp), 1);

  EXPECT_EQ(resp.status(), daemon::JOB_STATUS_FAILED);

  EXPECT_NE(resp.standard_response().error().message().find("Missing main"),
            std::string::npos);
}

TEST_F(HandleMeasureTest, LuaAssertionFailureReturnsError) {
  StartMockInstrument();

  auto script = write_script(R"(
    function main(ctx)
      assert(false, "domain validation failed")
    end
  )");

  daemon::MeasureJobRequest req;
  req.set_script_path(script.string());

  daemon::MeasureJobResultResponse resp;

  EXPECT_EQ(daemon::handle_measure(req, &resp), 1);

  EXPECT_FALSE(resp.standard_response().ok());

  EXPECT_NE(resp.standard_response().error().message().find(
                "domain validation failed"),
            std::string::npos);
}

TEST_F(HandleMeasureTest, DomainArgumentPassedToLua) {
  StartMockInstrument();

  auto script = write_script(R"(
    function main(ctx, domain)
      assert(domain:get_min() == 1.0)
      assert(domain:get_max() == 5.0)

      ctx:log("domain validated")
    end
  )");

  daemon::MeasureJobRequest req;
  req.set_script_path(script.string());

  auto *param = req.mutable_type_manifest()->add_parameters();

  param->set_name("domain");
  param->set_type(daemon::LUA_TYPES_DOMAIN);

  daemon::VariableValue value;
  const InstrumentDomain *domain = instrument_domain_create(1.0, 5.0);
  value.set_s(instrument_domain_serialize(domain));

  (*req.mutable_globals()->mutable_map())["domain"] = value;

  daemon::MeasureJobResultResponse resp;

  EXPECT_EQ(daemon::handle_measure(req, &resp), 0);

  auto log = test::read_log(log_path_);
  log.contains("domain validated");
}

TEST_F(HandleMeasureTest, DomainArrayPassedToLua) {
  StartMockInstrument();

  auto script = write_script(R"(
    function main(ctx, domains)
      assert(#domains == 2)

      assert(domains[1]:get_min() == 1.0)
      assert(domains[1]:get_max() == 2.0)

      assert(domains[2]:get_min() == 3.0)
      assert(domains[2]:get_max() == 4.0)

      ctx:log("domain array validated")
    end
  )");

  daemon::MeasureJobRequest req;
  req.set_script_path(script.string());

  auto *param = req.mutable_type_manifest()->add_parameters();

  param->set_name("domains");
  param->set_type(daemon::LUA_TYPES_DOMAIN_ARRAY);

  daemon::VariableValue value;

  auto *domains = value.mutable_dn_array();

  const InstrumentDomain *d1 = instrument_domain_create(1.0, 2.0);

  const InstrumentDomain *d2 = instrument_domain_create(3.0, 4.0);

  domains->add_values(instrument_domain_serialize(d1));
  domains->add_values(instrument_domain_serialize(d2));

  (*req.mutable_globals()->mutable_map())["domains"] = value;

  daemon::MeasureJobResultResponse resp;

  EXPECT_EQ(daemon::handle_measure(req, &resp), 0);
}

TEST_F(HandleMeasureTest, TargetArgumentPassedToLua) {
  StartMockInstrument();

  auto script = write_script(R"(
    function main(ctx, target)
      assert(
        target:get_instrument_name() ==
        "MockInstrument1")

      assert(target:get_channel() == 5)
      assert(target:get_channel_group() == "analog")

      ctx:log("target validated")
    end
  )");

  daemon::MeasureJobRequest req;
  req.set_script_path(script.string());

  auto *param = req.mutable_type_manifest()->add_parameters();

  param->set_name("target");
  param->set_type(daemon::LUA_TYPES_TARGET);

  daemon::VariableValue value;

  const InstrumentTarget *target =
      instrument_target_create("MockInstrument1", "analog", 5);
  value.set_s(instrument_target_serialize(target));

  (*req.mutable_globals()->mutable_map())["target"] = value;

  daemon::MeasureJobResultResponse resp;

  EXPECT_EQ(daemon::handle_measure(req, &resp), 0);

  auto log = test::read_log(log_path_);
  log.contains("target validated");
}

TEST_F(HandleMeasureTest, TargetArrayPassedToLua) {
  StartMockInstrument();

  auto script = write_script(R"(
    function main(ctx, targets)
      assert(#targets == 2)

      assert(
        targets[1]:get_instrument_name() ==
        "MockInstrument1")

      assert(targets[1]:get_channel() == 5)
      assert(targets[1]:get_channel_group() == "analog")

      assert(
        targets[2]:get_instrument_name() ==
        "MockInstrument2")

      assert(targets[2]:get_channel() == 7)
      assert(targets[2]:get_channel_group() == "digital")

      ctx:log("target array validated")
    end
  )");

  daemon::MeasureJobRequest req;
  req.set_script_path(script.string());

  auto *param = req.mutable_type_manifest()->add_parameters();

  param->set_name("targets");
  param->set_type(daemon::LUA_TYPES_TARGET_ARRAY);

  daemon::VariableValue value;

  auto *targets = value.mutable_t_array();

  const InstrumentTarget *t1 =
      instrument_target_create("MockInstrument1", "analog", 5);

  const InstrumentTarget *t2 =
      instrument_target_create("MockInstrument2", "digital", 7);

  targets->add_values(instrument_target_serialize(t1));

  targets->add_values(instrument_target_serialize(t2));

  (*req.mutable_globals()->mutable_map())["targets"] = value;

  daemon::MeasureJobResultResponse resp;

  EXPECT_EQ(daemon::handle_measure(req, &resp), 0);
  EXPECT_TRUE(resp.standard_response().ok());
  EXPECT_EQ(resp.status(), daemon::JOB_STATUS_COMPLETED);

  auto log = test::read_log(log_path_);
  log.contains("target array validated");
}

TEST_F(HandleMeasureTest, MissingRequiredParameterReturnsError) {
  StartMockInstrument();

  auto script = write_script(R"(
      function main(ctx, domain)
      end
  )");

  daemon::MeasureJobRequest req;
  req.set_script_path(script.string());

  auto *param = req.mutable_type_manifest()->add_parameters();

  param->set_name("domain");
  param->set_type(daemon::LUA_TYPES_DOMAIN);

  daemon::MeasureJobResultResponse resp;

  EXPECT_EQ(daemon::handle_measure(req, &resp), 1);
  EXPECT_NE(resp.standard_response().error().message().find(
                "Missing required parameter"),
            std::string::npos);
}

TEST_F(HandleMeasureTest, UnusedGlobalProducesWarning) {
  StartMockInstrument();

  auto script = write_script(R"(
    function main(ctx)
    end
  )");

  daemon::MeasureJobRequest req;
  req.set_script_path(script.string());

  daemon::VariableValue value;
  value.set_i(42);

  (*req.mutable_globals()->mutable_map())["unused"] = value;

  daemon::MeasureJobResultResponse resp;

  EXPECT_EQ(daemon::handle_measure(req, &resp), 0);

  auto log = test::read_log(log_path_);

  log.contains("Global variable 'unused' provided but not used");
}
