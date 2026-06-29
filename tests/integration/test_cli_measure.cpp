#include "test_cli_helpers.hpp"

class CLITestMeasure : public ::testing::Test {
protected:
  void SetUp() override {
    run_iss("daemon start --json");
    ASSERT_TRUE(wait_for_daemon_started())
        << "Daemon never became reachable in SetUp";
  }
  void TearDown() override {
    run_iss("inst stop MockInstrument1");
    run_iss("inst stop MockInstrument2");
    auto [exit_code, output] = run_iss("daemon stop");
    EXPECT_EQ(exit_code, 0) << "daemon stop failed:\n" << output;
    EXPECT_TRUE(wait_for_daemon_stopped(5000)) << "Daemon never stopped";
  }
};

TEST_F(CLITestMeasure, SimpleMeasure) {
  start_mock1();

  auto [exit_code, out] = run_iss(
      "measure " +
      (std::filesystem::path(data_dir) / "test_scripts" / "simple_call.lua")
          .string() +
      " --json");
  EXPECT_EQ(exit_code, 0) << "measure returned non-zero:\n" << out;
  EXPECT_NE(out.find("Measurement complete"), std::string::npos)
      << "Expected 'Measurement complete':\n"
      << out;

  stop_mock1();
}

TEST_F(CLITestMeasure, MeasureJsonOutputHasResultFields) {
  start_mock1();

  auto [exit_code, out] = run_iss("measure " +
                                  (std::filesystem::path(data_dir) /
                                   "test_scripts" / "multiple_returns.lua")
                                      .string() +
                                  " --json");
  ASSERT_EQ(exit_code, 0) << "measure --json failed:\n" << out;

  nlohmann::json j;
  ASSERT_NO_THROW(j = nlohmann::json::parse(out))
      << "Output is not valid JSON:\n"
      << out;

  EXPECT_TRUE(j.value("ok", false)) << j.dump(2);
  EXPECT_TRUE(j.contains("output")) << "JSON missing output: \n" << j.dump(2);
  ASSERT_TRUE(j["output"].back().contains("status"))
      << "JSON missing status field :\n"
      << j.dump(2);
  ASSERT_TRUE(j["output"].back()["status"] == "JOB_STATUS_COMPLETED")
      << "JSON status field incorrect:\n"
      << j["output"].back()["status"].dump(2);
  ASSERT_TRUE(j["output"].back().contains("results") &&
              j["output"].back()["results"].is_array())
      << "JSON missing 'results' array:\n"
      << j.dump(2);

  for (const auto &r : j["results"]) {
    EXPECT_TRUE(r.contains("instrument"))
        << "Result entry missing 'instrument': " << r;
    EXPECT_TRUE(r.contains("verb")) << "Result entry missing 'verb': " << r;
  }

  stop_mock1();
}

TEST_F(CLITestMeasure, LoopMeasurementCompletes) {
  start_mock1();

  auto [exit_code, out] = run_iss("measure " +
                                  (std::filesystem::path(data_dir) /
                                   "test_scripts" / "loop_measurement.lua")
                                      .string() +
                                  " --json");
  EXPECT_EQ(exit_code, 0) << "Loop measurement failed:\n" << out;
  EXPECT_NE(out.find("Measurement complete"), std::string::npos)
      << "Expected 'Measurement complete':\n"
      << out;

  stop_mock1();
}

TEST_F(CLITestMeasure, MeasureNonExistentScriptFails) {
  auto [exit_code, out] = run_iss(
      "measure " +
      (std::filesystem::path(data_dir) / "tmp" / "does_not_exist_xyz_1234.lua")
          .string() +
      " --json");
  EXPECT_NE(exit_code, 0) << "Expected failure for missing script:\n" << out;
}

TEST_F(CLITestMeasure, TwoInstrumentMeasureCompletes) {
  start_mock1();
  start_mock2();

  auto [exit_code, out] = run_iss(
      "measure " +
      (std::filesystem::path(data_dir) / "test_scripts" / "two_instruments.lua")
          .string() +
      " --json");
  EXPECT_EQ(exit_code, 0) << "Two-instrument measurement failed:\n" << out;
  EXPECT_NE(out.find("Measurement complete"), std::string::npos)
      << "Expected 'Measurement complete':\n"
      << out;

  nlohmann::json j;
  ASSERT_NO_THROW(j = nlohmann::json::parse(out))
      << "Output is not valid JSON:\n"
      << out;

  ASSERT_TRUE(j.contains("output")) << "JSON missing output:\n" << j.dump(2);
  ASSERT_TRUE(j["output"].is_array()) << "JSON output is not array:\n"
                                      << j.dump(2);
  ASSERT_FALSE(j["output"].empty()) << "JSON output is empty:\n" << j.dump(2);

  const auto &last = j["output"].back();

  ASSERT_TRUE(last.contains("results")) << "JSON missing results:\n"
                                        << j.dump(2);

  ASSERT_TRUE(last["results"].is_array()) << "results is not array:\n"
                                          << j.dump(2);

  const auto &results = last["results"];

  ASSERT_GE(results.size(), 2) << "Expected at least 2 results:\n" << j.dump(2);

  std::vector<std::string> names;
  for (const auto &r : results) {
    ASSERT_TRUE(r.contains("instrumentName"))
        << "Missing instrumentName field:\n"
        << r.dump(2);

    names.push_back(r["instrumentName"].get<std::string>());
  }

  auto has_name = [&](const std::string &name) {
    return std::ranges::find(names, name) != names.end();
  };

  EXPECT_TRUE(has_name("MockInstrument1")) << "Missing MockInstrument1";
  EXPECT_TRUE(has_name("MockInstrument2")) << "Missing MockInstrument2";

  stop_mock1();
  stop_mock2();
}
