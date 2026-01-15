#include "instrument-server/server/JobManager.hpp"
#include "instrument-server/Logger.hpp"
#include "instrument-server/server/RuntimeContext.hpp"
#include <algorithm>
#include <chrono>
#include <sol/sol.hpp>
#include <sstream>
#include <thread>

using json = nlohmann::json;

namespace instserver {
namespace server {

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
    if (!running_)
      return;
    running_ = false;
  }
  cv_.notify_all();
  if (worker_thread_.joinable())
    worker_thread_.join();
  LOG_INFO("JOB", "MGR", "JobManager stopped");
}

std::string JobManager::make_job_id() {
  uint64_t n = next_id_.fetch_add(1);
  auto now = std::chrono::system_clock::now();
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch())
                .count();
  std::ostringstream ss;
  ss << "job-" << ms << "-" << n;
  return ss.str();
}

std::string JobManager::submit_job(const std::string &job_type,
                                   const json &params) {
  JobInfo info;
  info.id = make_job_id();
  info.type = job_type;
  info.params = params;
  info.status = "queued";
  info.created_at = std::chrono::system_clock::now();

  {
    std::lock_guard<std::mutex> lk(mutex_);
    jobs_.emplace(info.id, info);
    queue_.push_back(info.id);
  }
  cv_.notify_one();

  LOG_INFO("JOB", "SUBMIT", "Submitted job {} type={}", info.id, job_type);
  return info.id;
}

std::string JobManager::submit_measure(const std::string &script_path,
                                       const json &params) {
  json p = params;
  p["script_path"] = script_path;
  return submit_job("measure", p);
}

bool JobManager::get_job_info(const std::string &job_id, JobInfo &out) {
  std::lock_guard<std::mutex> lk(mutex_);
  auto it = jobs_.find(job_id);
  if (it == jobs_.end())
    return false;
  out = it->second;
  return true;
}

bool JobManager::get_job_result(const std::string &job_id, json &out) {
  std::lock_guard<std::mutex> lk(mutex_);
  auto it = jobs_.find(job_id);
  if (it == jobs_.end())
    return false;
  if (it->second.status != "completed")
    return false;
  out = it->second.result;
  return true;
}

std::vector<JobInfo> JobManager::list_jobs() {
  std::vector<JobInfo> v;
  std::lock_guard<std::mutex> lk(mutex_);
  v.reserve(jobs_.size());
  for (auto &kv : jobs_)
    v.push_back(kv.second);
  return v;
}

bool JobManager::cancel_job(const std::string &job_id) {
  std::lock_guard<std::mutex> lk(mutex_);
  auto it = jobs_.find(job_id);
  if (it == jobs_.end())
    return false;
  // If queued, remove from queue and mark canceled
  if (it->second.status == "queued") {
    auto qit = std::find(queue_.begin(), queue_.end(), job_id);
    if (qit != queue_.end())
      queue_.erase(qit);
    it->second.status = "canceled";
    it->second.finished_at = std::chrono::system_clock::now();
    it->second.error = "canceled";
    return true;
  }
  // If running, set status to canceled - cooperation required
  if (it->second.status == "running") {
    it->second.status = "canceling";
    // Worker should check status and abort if possible.
    return true;
  }
  // If already finished, cannot cancel
  return false;
}

void JobManager::worker_loop() {
  while (true) {
    std::string jid;
    {
      std::unique_lock<std::mutex> lk(mutex_);
      cv_.wait(lk, [this]() { return !queue_.empty() || !running_; });
      if (!running_ && queue_.empty())
        break;
      if (!queue_.empty()) {
        // Before popping a non-measure job, wait for active measure jobs to
        // finish. Peek at the front to see its type.
        jid = queue_.front();
        auto &jpeek = jobs_.at(jid);
        if (jpeek.type != "measure") {
          // Wait until there are no active measure jobs before proceeding.
          while (!active_measure_jobs_.empty() && running_) {
            LOG_DEBUG("JOB", "LOOP",
                      "Waiting for active measure jobs to finish before "
                      "running non-measure job");
            measure_cv_.wait(lk);
          }
        }

        // Pop and set running
        queue_.pop_front();
        auto &j = jobs_.at(jid);
        j.status = "running";
        j.started_at = std::chrono::system_clock::now();
      }
    }

    if (jid.empty())
      continue;

    LOG_INFO("JOB", "RUN", "Starting job {}", jid);

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
      if (run_info.type == "sleep") {
        // params: duration_ms
        int ms = 100;
        try {
          ms = run_info.params.value("duration_ms", 100);
        } catch (...) {
        }
        // Check canceling requests periodically
        int slept = 0;
        const int step = 20;
        while (slept < ms) {
          {
            std::lock_guard<std::mutex> lk(mutex_);
            auto it = jobs_.find(jid);
            if (it != jobs_.end() && (it->second.status == "canceling" ||
                                      it->second.status == "canceled")) {
              throw std::runtime_error("canceled");
            }
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(step));
          slept += step;
        }
        result["message"] = "slept";
        result["duration_ms"] = ms;
        success = true;
      } else if (run_info.type == "measure") {
        // Enqueue-first behavior:
        // 1) Create a Lua state, bind a RuntimeContext in enqueue_mode=true
        // 2) Run the script to parse and enqueue commands quickly
        // 3) Spawn a monitor thread that waits for the context's tokens to be
        //    processed and futures to complete; the worker loop continues to
        //    next job.

        std::string script_path = run_info.params.value("script_path", "");
        if (script_path.empty()) {
          throw std::runtime_error("missing script_path");
        }

        // Prepare Lua state and runtime context (enqueue mode)
        sol::state lua;
        lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::table,
                           sol::lib::string, sol::lib::io, sol::lib::os);

        SyncCoordinator sync_coordinator;
        auto ctx = bind_runtime_context(lua, InstrumentRegistry::instance(),
                                        sync_coordinator, true);

        // Run script to parse and enqueue commands (this may block on parallel
        // blocks)
        auto load_result = lua.safe_script_file(script_path);
        if (!load_result.valid()) {
          sol::error err = load_result;
          throw std::runtime_error(std::string("Script error: ") + err.what());
        }

        // Mark this measure job active
        {
          std::lock_guard<std::mutex> lk(mutex_);
          active_measure_jobs_.insert(jid);
        }

        // Spawn monitor thread to wait for enqueued commands to complete.
        std::thread monitor([jid, ctx]() mutable {
          LOG_INFO("JOB", "MON", "Monitoring job {}", jid);
          // Release tokens in order and wait for command completion
          ctx->process_tokens_and_wait();

          // Collect results JSON
          json r = ctx->collect_results_json();

          // Update job record and mark inactive
          auto &mgr = JobManager::instance();
          {
            std::lock_guard<std::mutex> lk(mgr.mutex_);
            auto it = mgr.jobs_.find(jid);
            if (it != mgr.jobs_.end()) {
              it->second.result = r;
              it->second.status = "completed";
              it->second.finished_at = std::chrono::system_clock::now();
              LOG_INFO("JOB", "MON", "Job {} completed (monitor)", jid);
            }
            // Remove from active measure jobs and notify waiting non-measure
            // jobs
            mgr.active_measure_jobs_.erase(jid);
          }
          mgr.measure_cv_.notify_all();
        });

        monitor.detach();

        // Mark success for now (actual completion will be done by monitor)
        success = true;
        result["message"] = "enqueued";
      } else {
        // unknown job type
        throw std::runtime_error("unknown job type: " + run_info.type);
      }
    } catch (const std::exception &e) {
      success = false;
      err = e.what();
    } catch (...) {
      success = false;
      err = "unknown exception";
    }

    {
      std::lock_guard<std::mutex> lk(mutex_);
      auto it = jobs_.find(jid);
      if (it != jobs_.end()) {
        if (run_info.type != "measure") {
          // For non-measure jobs we set final status here
          if (success) {
            it->second.status = "completed";
            it->second.result = result;
          } else {
            it->second.status = "failed";
            it->second.error = err;
          }
          it->second.finished_at = std::chrono::system_clock::now();
        } else {
          // measure: monitor thread will mark completion; leave as
          // running/enqueued
          if (!success) {
            it->second.status = "failed";
            it->second.error = err;
            it->second.finished_at = std::chrono::system_clock::now();
            // remove from active set if failed at enqueue time
            active_measure_jobs_.erase(jid);
            measure_cv_.notify_all();
          } else {
            // it->second.status remains "running" while monitor works
          }
        }
      }
    }

    LOG_INFO("JOB", "DONE", "Job {} dispatched (type={})", jid, run_info.type);
  }
}

} // namespace server
} // namespace instserver
