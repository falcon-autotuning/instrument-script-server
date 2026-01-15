#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>

namespace instserver {
namespace server {

struct JobInfo {
  std::string id;
  std::string type;      // e.g., "measure", "sleep"
  nlohmann::json params; // job-specific parameters
  std::string status;    // "queued","running","completed","failed","canceled"
  nlohmann::json result; // result JSON when completed
  std::string error;
  std::chrono::system_clock::time_point created_at;
  std::chrono::system_clock::time_point started_at;
  std::chrono::system_clock::time_point finished_at;
};

class JobManager {
public:
  static JobManager &instance();

  // Submit a generic job type. Returns job id.
  std::string submit_job(const std::string &job_type,
                         const nlohmann::json &params);

  // Submit a measure job convenience wrapper
  std::string submit_measure(const std::string &script_path,
                             const nlohmann::json &params);

  // Query job info (returns false if job id not found)
  bool get_job_info(const std::string &job_id, JobInfo &out);

  // Fetch result JSON (returns false if not found or not completed)
  bool get_job_result(const std::string &job_id, nlohmann::json &out);

  // List all jobs (returns copy)
  std::vector<JobInfo> list_jobs();

  // Attempt to cancel a job (only works if queued or running; running
  // cancellation is cooperative)
  bool cancel_job(const std::string &job_id);

  // Stop worker thread and cleanup. Safe to call multiple times.
  void stop();

private:
  JobManager();
  ~JobManager();

  // Non-copyable
  JobManager(const JobManager &) = delete;
  JobManager &operator=(const JobManager &) = delete;

  void worker_loop();
  std::string make_job_id();

  std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<std::string> queue_; // job ids queued
  std::unordered_map<std::string, JobInfo> jobs_;
  std::atomic<uint64_t> next_id_{1};
  bool running_;
  std::thread worker_thread_;

  // Active measure jobs (ids). Non-measure jobs wait until this set is empty.
  std::set<std::string> active_measure_jobs_;
  std::condition_variable measure_cv_;
};

} // namespace server
} // namespace instserver
