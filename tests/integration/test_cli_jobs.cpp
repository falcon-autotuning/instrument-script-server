#include "test_cli_helpers.hpp"

class CLITestJobs : public ::testing::Test {
protected:
  void SetUp() override {
    run_iss("daemon start --json");
    ASSERT_TRUE(wait_for_daemon_started())
        << "Daemon never became reachable in SetUp";
  }
  void TearDown() override {
    run_iss("inst stop MockInstrument1");
    auto [exit_code, output] = run_iss("daemon stop");
    EXPECT_EQ(exit_code, 0) << "daemon stop failed:\n" << output;
    EXPECT_TRUE(wait_for_daemon_stopped(5000)) << "Daemon never stopped";
  }

  uint32_t extract_job_id(const std::string &out) {
    auto pos = out.find("job_id=");
    if (pos == std::string::npos) return 0;
    auto end = out.find(")", pos);
    if (end == std::string::npos) return 0;
    try {
      return std::stoul(out.substr(pos + 7, end - (pos + 7)));
    } catch (...) {
      return 0;
    }
  }
};

TEST_F(CLITestJobs, JobMeasureStatusListAndResult) {
  start_mock1();

  // Enqueue job
  auto [exit_code, out] = run_iss(
      "job measure " +
      (std::filesystem::path(data_dir) / "test_scripts" / "simple_call.lua")
          .string());
  EXPECT_EQ(exit_code, 0) << "job measure failed:\n" << out;
  uint32_t job_id = extract_job_id(out);
  ASSERT_GT(job_id, 0) << "Failed to extract job ID from: " << out;

  // Poll job status until complete (status 3 = JOB_STATUS_COMPLETED)
  bool finished = false;
  for (int i = 0; i < 30; ++i) {
    auto [status_rc, status_out] = run_iss("job status " + std::to_string(job_id));
    EXPECT_EQ(status_rc, 0);
    if (status_out.find("Status: 3") != std::string::npos) {
      finished = true;
      break;
    }
    std::this_thread::sleep_for(100ms);
  }
  EXPECT_TRUE(finished) << "Job did not complete in time";

  // Check job list
  auto [list_rc, list_out] = run_iss("job list");
  EXPECT_EQ(list_rc, 0);
  EXPECT_NE(list_out.find(std::to_string(job_id)), std::string::npos)
      << "Job ID " << job_id << " not found in list: " << list_out;

  // Check job result
  auto [res_rc, res_out] = run_iss("job result " + std::to_string(job_id));
  EXPECT_EQ(res_rc, 0);
  EXPECT_NE(res_out.find("Status: 3"), std::string::npos);
  EXPECT_NE(res_out.find("MockInstrument1"), std::string::npos);

  stop_mock1();
}

TEST_F(CLITestJobs, CancelLongRunningJob) {
  start_mock1();

  // Enqueue a long running job (approx 1s)
  auto [exit_code, out] = run_iss(
      "job measure " +
      (std::filesystem::path(data_dir) / "test_scripts" / "long_running_job.lua")
          .string());
  EXPECT_EQ(exit_code, 0) << "job measure failed:\n" << out;
  uint32_t job_id = extract_job_id(out);
  ASSERT_GT(job_id, 0);

  // Immediately cancel job
  auto [cancel_rc, cancel_out] = run_iss("job cancel " + std::to_string(job_id));
  EXPECT_EQ(cancel_rc, 0) << "job cancel failed:\n" << cancel_out;
  EXPECT_NE(cancel_out.find("Cancelled job"), std::string::npos);

  // Verify status is JOB_STATUS_CANCELLED (6) or JOB_STATUS_CANCELING (5)
  auto [status_rc, status_out] = run_iss("job status " + std::to_string(job_id));
  EXPECT_EQ(status_rc, 0);
  bool is_canceled = (status_out.find("Status: 6") != std::string::npos) ||
                     (status_out.find("Status: 5") != std::string::npos);
  EXPECT_TRUE(is_canceled) << "Job was not cancelled: " << status_out;

  stop_mock1();
}

TEST_F(CLITestJobs, MultipleConcurrentJobs) {
  start_mock1();

  auto [exit_code1, out1] = run_iss(
      "job measure " +
      (std::filesystem::path(data_dir) / "test_scripts" / "simple_call.lua")
          .string());
  uint32_t job_id1 = extract_job_id(out1);
  ASSERT_GT(job_id1, 0);

  auto [exit_code2, out2] = run_iss(
      "job measure " +
      (std::filesystem::path(data_dir) / "test_scripts" / "simple_call.lua")
          .string());
  uint32_t job_id2 = extract_job_id(out2);
  ASSERT_GT(job_id2, 0);

  // Verify both exist in list
  auto [list_rc, list_out] = run_iss("job list");
  EXPECT_EQ(list_rc, 0);
  EXPECT_NE(list_out.find(std::to_string(job_id1)), std::string::npos);
  EXPECT_NE(list_out.find(std::to_string(job_id2)), std::string::npos);

  stop_mock1();
}
