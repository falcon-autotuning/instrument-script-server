#pragma once

#include "instserver/daemon/v1/daemon_messages.pb.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <nlohmann/json.hpp>
#include <set>
#include <thread>
#include <unordered_map>

namespace instserver::daemon {
using JobID = uint32_t;
struct SleepRequest {
  uint64_t duration_ms = 0;
};
struct SleepResponse {};
using JobParams = std::variant<v1::MeasureJobRequest, SleepRequest>;
using JobResults = std::variant<v1::MeasureJobResultResponse, SleepResponse>;
struct JobInfo {
  v1::Job job;
  JobParams params;  // job-specific parameters
  JobResults result; // result JSON when completed
};

class JobManager {
public:
  static JobManager &instance();

  // Submit a generic job type. Returns job id.
  JobID submit_job(v1::JobType, const JobParams &);

  // Submit a measure job convenience wrapper
  JobID submit_measure(const v1::MeasureJobRequest &);

  // Query job info (returns false if job id not found)
  bool get_job_info(JobID, JobInfo &);

  // Fetch result JSON (returns false if not found or not completed)
  bool get_job_result(JobID, JobResults &);

  // List all jobs (returns copy)
  std::unordered_map<JobID, JobInfo> list_jobs();

  // Attempt to cancel a job (only works if queued or running; running
  // cancellation is cooperative)
  bool cancel_job(JobID);

  // Stop worker thread and cleanup. Safe to call multiple times.
  void stop();

private:
  JobManager();
  ~JobManager();

  // Non-copyable
  JobManager(const JobManager &) = delete;
  JobManager &operator=(const JobManager &) = delete;

  void worker_loop();
  JobID make_job_id();

  std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<JobID> queue_; // job ids queued
  std::unordered_map<JobID, JobInfo> jobs_;
  std::atomic<JobID> next_id_{1};
  bool running_;
  std::thread worker_thread_;

  // Active measure jobs (ids). Non-measure jobs wait until this set is empty.
  std::set<JobID> active_measure_jobs_;
  std::condition_variable measure_cv_;
};

} // namespace instserver::daemon
