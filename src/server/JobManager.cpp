#include "instrument-script-server/server/JobManager.hpp"
#include "instrument-script-server/server/CommandHandlers.hpp"
#include "instserver/server/v1/daemon_messages.pb.h"
#include <algorithm>
#include <chrono>
#include <fmt/format.h>
#include <google/protobuf/util/time_util.h>
#include <instrument-log/inst_logging.h>
#include <sol/sol.hpp>
#include <stdexcept>
#include <thread>

using json = nlohmann::json;

namespace instserver::server {

JobManager &JobManager::instance() {
  static JobManager mgr;
  return mgr;
}

JobManager::JobManager()
    : running_(true), worker_thread_(&JobManager::worker_loop, this) {
  LOG_INFO("JOB", "MGR", "JobManager started");
}

JobManager::~JobManager() { stop(); }

void JobManager::stop() {
  {
    std::lock_guard<std::mutex> lk(mutex_);
    if (!running_) {
      return;
    }
    running_ = false;
  }
  cv_.notify_all();
  if (worker_thread_.joinable()) {
    worker_thread_.join();
  }
  LOG_INFO("JOB", "MGR", "JobManager stopped");
}

JobID JobManager::make_job_id() { return next_id_.fetch_add(1); }

JobID JobManager::submit_job(v1::JobType job_type, const JobParams &params) {
  JobID jid = make_job_id();
  JobInfo info;
  info.job.set_type(job_type);
  info.params = params;
  info.job.set_status(v1::JOB_STATUS_QUEUED);
  *info.job.mutable_created_at() =
      google::protobuf::util::TimeUtil::GetCurrentTime();

  {
    std::lock_guard<std::mutex> lk(mutex_);
    jobs_.emplace(jid, info);
    queue_.push_back(jid);
  }
  cv_.notify_one();

  LOG_INFO("JOB", "SUBMIT", "Submitted job %d type=%s", jid,
           JobType_Name(job_type).c_str());
  return jid;
}

JobID JobManager::submit_measure(const v1::MeasureJobRequest &params) {
  return submit_job(v1::JOB_TYPE_MEASURE, params);
}

bool JobManager::get_job_info(uint32_t job_id, JobInfo &out) {
  std::lock_guard<std::mutex> lk(mutex_);
  auto it = jobs_.find(job_id);
  if (it == jobs_.end()) {
    return false;
  }
  out = it->second;
  return true;
}

bool JobManager::get_job_result(JobID job_id, JobResults &out) {
  std::lock_guard<std::mutex> lk(mutex_);
  auto it = jobs_.find(job_id);
  if (it == jobs_.end()) {
    return false;
  }
  if (it->second.job.status() != v1::JOB_STATUS_COMPLETED) {
    return false;
  }
  out = it->second.result;
  return true;
}

std::unordered_map<JobID, JobInfo> JobManager::list_jobs() {
  std::vector<JobInfo> v;
  std::lock_guard<std::mutex> lk(mutex_);
  return jobs_;
}

bool JobManager::cancel_job(JobID job_id) {
  std::lock_guard<std::mutex> lk(mutex_);
  auto it = jobs_.find(job_id);
  if (it == jobs_.end()) {
    return false;
  }
  // If queued, remove from queue and mark canceled
  if (it->second.job.status() == v1::JOB_STATUS_QUEUED) {
    auto qit = std::ranges::find(queue_, job_id);
    if (qit != queue_.end()) {
      queue_.erase(qit);
    }
    it->second.job.set_status(v1::JOB_STATUS_CANCELLED);
    *it->second.job.mutable_finished_at() =
        google::protobuf::util::TimeUtil::GetCurrentTime();
    return true;
  }
  // If running, set status to canceled - cooperation required
  if (it->second.job.status() == v1::JOB_STATUS_RUNNING) {
    it->second.job.set_status(v1::JOB_STATUS_CANCELING);
    // Worker should check status and abort if possible.
    return true;
  }
  // If already finished, cannot cancel
  return false;
}

void JobManager::worker_loop() {
  while (running_) {
    JobID jid = 0;
    {
      std::unique_lock<std::mutex> lk(mutex_);
      cv_.wait(lk, [this]() { return !queue_.empty() || !running_; });
      if (!running_ && queue_.empty()) {
        break;
      }
      if (!queue_.empty()) {
        // Pop and set running (blocking mode - jobs execute sequentially)
        jid = queue_.front();
        queue_.pop_front();
        auto &j = jobs_.at(jid);
        j.job.set_status(v1::JOB_STATUS_RUNNING);
        *j.job.mutable_started_at() =
            google::protobuf::util::TimeUtil::GetCurrentTime();
      }
    }

    LOG_INFO("JOB", "RUN", "Starting job %d", jid);

    // Execute based on job type
    bool success = false;
    json result;
    std::string err;

    JobInfo run_info;
    {
      std::lock_guard<std::mutex> lk(mutex_);
      run_info = jobs_.at(jid);
    }

    try {
      switch (run_info.job.type()) {

      case v1::JOB_TYPE_SLEEP: {
        auto sleep_req = std::get<SleepRequest>(run_info.params);

        SleepResponse sleep_resp;

        int ms = sleep_req.duration_ms;
        int slept = 0;
        const int step = 20;

        while (slept < ms) {
          {
            std::lock_guard<std::mutex> lk(mutex_);
            auto it = jobs_.find(jid);
            if (it != jobs_.end() &&
                (it->second.job.status() == v1::JOB_STATUS_CANCELING ||
                 it->second.job.status() == v1::JOB_STATUS_CANCELLED)) {
              throw std::runtime_error("canceled");
            }
          }

          std::this_thread::sleep_for(std::chrono::milliseconds(step));
          slept += step;
        }

        run_info.result = sleep_resp;
        success = true;
        break;
      }

      case v1::JOB_TYPE_MEASURE: {
        const auto &req = std::get<v1::MeasureJobRequest>(run_info.params);

        v1::MeasureJobResultResponse resp;

        int rc = handle_measure(req, &resp);

        if (rc != 0) {
          throw std::runtime_error("measure job failed");
        }

        run_info.result = resp;
        success = true;
        break;
      }

      default:
        throw std::runtime_error(
            "unknown job type: " +
            std::string(v1::JobType_Name(run_info.job.type())));
      }

    } catch (const std::exception &e) {
      success = false;
      err = e.what();
    }

    {
      std::lock_guard<std::mutex> lk(mutex_);
      auto it = jobs_.find(jid);
      if (it != jobs_.end()) {
        // Update status based on success
        if (success) {
          it->second.job.set_status(v1::JOB_STATUS_COMPLETED);
          it->second.job.set_result(result);
        } else {
          it->second.job.set_status(v1::JOB_STATUS_FAILED);
        }
        *it->second.job.mutable_finished_at() =
            google::protobuf::util::TimeUtil::GetCurrentTime();
      }
    }

    LOG_INFO("JOB", "DONE", "Job %d dispatched (type=%s)", jid,
             JobType_Name(run_info.job.type()).c_str());
  }
}

} // namespace instserver::server
