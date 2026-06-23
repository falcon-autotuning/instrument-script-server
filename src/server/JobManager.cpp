#include "instrument-script-server/server/JobManager.hpp"
#include "instrument-script-server/server/CommandHandlers.hpp"
#include "instrument-script-server/server/InstrumentRegistry.hpp"
#include "instrument-script-server/server/RuntimeContext.hpp"
#include "instrument-script-server/server/ServerDaemon.hpp"
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
  while (true) {
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
        // params: duration_ms
        int ms = 100;
        try {
          SleepRequest sleep_req;
          sleep_req.duration_ms = ms;
          run_info.params = sleep_req;
        } catch (...) {
        }
        // Check canceling requests periodically
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
        success = true;
      }
      case v1::JOB_TYPE_MEASURE: {
        // Enqueue-first behavior:
        // 1) Create a Lua state, bind a RuntimeContext in enqueue_mode=true
        // 2) Run the script to parse and enqueue commands quickly
        // 3) Spawn a monitor thread that waits for the context's tokens to be
        //    processed and futures to complete; the worker loop continues to
        //    next job.
        auto &measure_req = std::get<v1::MeasureJobRequest>(run_info.params);
        std::string script_path = measure_req.script_path();
        if (script_path.empty()) {
          throw std::runtime_error("missing script_path");
        }

        // Prepare Lua state and runtime context (blocking mode -
        // enqueue_mode=false) This allows users to perform math on measurement
        // results in Lua
        sol::state lua;
        lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::table,
                           sol::lib::string, sol::lib::io, sol::lib::os);

        // Load optional Lua libraries (was missing before)
        load_optional_lua_libs(lua);

        auto &sync = ServerDaemon::instance().sync_coordinator();
        auto ctx =
            bind_runtime_context(lua, InstrumentRegistry::instance(), sync);

        // Load the script file
        auto load_result = lua.safe_script_file(script_path);
        if (!load_result.valid()) {
          sol::error err = load_result;
          throw std::runtime_error(std::string("Script load error: ") +
                                   err.what());
        }

        // Check if the script defined a main function (new format)
        sol::optional<sol::function> main_func = lua["main"];

        if (!main_func.has_value()) {
          std::string error = "Missing main(ctx, ...) function.";
          LOG_ERROR("JOB", "MEASURE", error.c_str());
          throw std::runtime_error(error);
        }
        // Check if type_manifest is provided (Teal static typing support)
        if (measure_req.has_type_manifest()) {
          const auto &manifest = measure_req.type_manifest();

          // Build arguments based on manifest
          std::vector<sol::object> args;
          args.push_back(
              sol::make_object(lua, ctx.get())); // First arg is always context

          const auto &param_defs = manifest.parameters();
          for (int i = 1; i < param_defs.size(); ++i) { // Skip first (context)
            const auto &param = param_defs[i];
            std::string param_name = param.name();

            // Check if this parameter exists in globals
            if (!measure_req.globals().map().contains(param_name)) {
              std::string error_msg =
                  fmt::format("Missing required parameter '{}' (declared in "
                              "type_manifest but not provided in globals)",
                              param_name);
              LOG_ERROR(
                  "JOB", "MEASURE",
                  "Missing required parameter '%s' for typed main function",
                  param_name.c_str());
              throw std::runtime_error(error_msg);
            }

            // Convert JSON value to Lua object
            sol::object arg = variable_to_lua(
                lua, &measure_req.globals().map().at(param_name));
            args.push_back(arg);

            LOG_INFO("JOB", "MEASURE",
                     "Passing parameter '%s' to main function (type: %s)",
                     param_name.c_str(), LuaTypes_Name(param.type()).c_str());
          }

          // Check for unused globals (warnings)
          for (const auto &it : measure_req.globals().map()) {
            std::string global_name = it.first;
            bool found = false;

            for (int i = 1; i < param_defs.size(); ++i) {
              if (param_defs[i].name() == global_name) {
                found = true;
                break;
              }
            }

            if (!found) {
              LOG_WARN("JOB", "MEASURE",
                       "Global variable '%s' provided but not used by "
                       "typed main function (injecting as global)",
                       global_name.c_str());
              // Still inject it as global for backward compatibility
              lua[global_name] = variable_to_lua(lua, &it.second);
            }
          }

          // Call main with unpacked arguments
          sol::protected_function_result main_result =
              (*main_func)(sol::as_args(args));

          if (!main_result.valid()) {
            sol::error err = main_result;
            std::string error_msg =
                std::string("Script execution error: ") + err.what();
            if (ctx->has_error()) {
              error_msg = ctx->get_error() + " (Runtime: " + err.what() + ")";
            }
            throw std::runtime_error(error_msg);
          }
        } else {
          // Legacy: call main with just context parameter
          sol::protected_function_result main_result = (*main_func)(ctx.get());

          if (!main_result.valid()) {
            sol::error err = main_result;
            std::string error_msg =
                std::string("Script execution error: ") + err.what();
            // If context:error() was also called, include both messages
            if (ctx->has_error()) {
              error_msg = ctx->get_error() + " (Runtime: " + err.what() + ")";
            }
            throw std::runtime_error(error_msg);
          }
        }

        // Check for explicit errors from context:error()
        if (ctx->has_error()) {
          throw std::runtime_error(ctx->get_error());
        }

        // Blocking mode: script has completed execution synchronously
        // Collect results directly
        result = ctx->collect_results_json();
        success = true;
      }
      default: {
        // unknown job type
        throw std::runtime_error("unknown job type: " +
                                 std::to_string(run_info.job.type()));
      }
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
