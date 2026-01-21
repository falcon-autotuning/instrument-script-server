#include "instrument-server/server/JobManager.hpp"
#include "instrument-server/Logger.hpp"
#include "instrument-server/server/RuntimeContext.hpp"
#include "instrument-server/server/CommandHandlers.hpp"
#include "instrument-server/server/InstrumentRegistry.hpp"
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
        // Pop and set running (blocking mode - jobs execute sequentially)
        jid = queue_.front();
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

        // Prepare Lua state and runtime context (blocking mode - enqueue_mode=false)
        // This allows users to perform math on measurement results in Lua
        sol::state lua;
        lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::table,
                           sol::lib::string, sol::lib::io, sol::lib::os);

        // Load optional Lua libraries (was missing before)
        load_optional_lua_libs(lua);

        SyncCoordinator sync_coordinator;
        auto ctx = bind_runtime_context(lua, InstrumentRegistry::instance(),
                                        sync_coordinator, false);

        // Load the script file
        auto load_result = lua.safe_script_file(script_path);
        if (!load_result.valid()) {
          sol::error err = load_result;
          throw std::runtime_error(std::string("Script load error: ") + err.what());
        }

        // Check if the script defined a main function (new format)
        sol::optional<sol::function> main_func = lua["main"];
        
        if (main_func) {
          // New format: call main function with context
          LOG_INFO("JOB", "MEASURE", "Executing script with main function (new format)");
          
          // Check if type_manifest is provided (Teal static typing support)
          if (run_info.params.contains("type_manifest")) {
            const auto &manifest = run_info.params["type_manifest"];
            
            // Validate manifest structure
            if (!manifest.contains("parameters") || !manifest["parameters"].is_array()) {
              throw std::runtime_error("Invalid type_manifest: missing or invalid 'parameters' array");
            }
            
            // Build arguments based on manifest
            std::vector<sol::object> args;
            args.push_back(sol::make_object(lua, ctx.get())); // First arg is always context
            
            const auto &param_defs = manifest["parameters"];
            for (size_t i = 1; i < param_defs.size(); ++i) { // Skip first (context)
              const auto &param = param_defs[i];
              
              if (!param.contains("name") || !param["name"].is_string()) {
                throw std::runtime_error(fmt::format("Invalid type_manifest: parameter {} missing 'name'", i));
              }
              
              std::string param_name = param["name"];
              
              // Check if this parameter exists in globals
              if (!run_info.params.contains("globals") || !run_info.params["globals"].contains(param_name)) {
                std::string error_msg = fmt::format(
                    "Missing required parameter '{}' (declared in type_manifest but not provided in globals)", 
                    param_name);
                LOG_ERROR("JOB", "MEASURE", 
                          "Missing required parameter '{}' for typed main function", param_name);
                throw std::runtime_error(error_msg);
              }
              
              // Convert JSON value to Lua object
              sol::object arg = json_to_lua(lua, run_info.params["globals"][param_name]);
              args.push_back(arg);
              
              LOG_INFO("JOB", "MEASURE", 
                       "Passing parameter '{}' to main function (type: {})", 
                       param_name, 
                       param.value("type", "unknown"));
            }
            
            // Check for unused globals (warnings)
            if (run_info.params.contains("globals")) {
              for (auto it = run_info.params["globals"].begin(); it != run_info.params["globals"].end(); ++it) {
                std::string global_name = it.key();
                bool found = false;
                
                for (size_t i = 1; i < param_defs.size(); ++i) {
                  if (param_defs[i]["name"] == global_name) {
                    found = true;
                    break;
                  }
                }
                
                if (!found) {
                  LOG_WARN("JOB", "MEASURE",
                           "Global variable '{}' provided but not used by typed main function (injecting as global)", 
                           global_name);
                  // Still inject it as global for backward compatibility
                  lua[global_name] = json_to_lua(lua, it.value());
                }
              }
            }
            
            // Call main with unpacked arguments
            sol::protected_function_result main_result = (*main_func)(sol::as_args(args));
            
            if (!main_result.valid()) {
              sol::error err = main_result;
              std::string error_msg = std::string("Script execution error: ") + err.what();
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
              std::string error_msg = std::string("Script execution error: ") + err.what();
              // If context:error() was also called, include both messages
              if (ctx->has_error()) {
                error_msg = ctx->get_error() + " (Runtime: " + err.what() + ")";
              }
              throw std::runtime_error(error_msg);
            }
          }
        } else {
          // Old format: script executed at load time (backward compatibility)
          LOG_WARN("JOB", "MEASURE",
                   "DEPRECATED: Script uses compatibility mode (no main function). "
                   "Please migrate to new format with main(ctx) function.");
        }

        // Check for explicit errors from context:error()
        if (ctx->has_error()) {
          throw std::runtime_error(ctx->get_error());
        }

        // Blocking mode: script has completed execution synchronously
        // Collect results directly
        result = ctx->collect_results_json();
        success = true;
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
        // Update status based on success
        if (success) {
          it->second.status = "completed";
          it->second.result = result;
        } else {
          it->second.status = "failed";
          it->second.error = err;
        }
        it->second.finished_at = std::chrono::system_clock::now();
      }
    }

    LOG_INFO("JOB", "DONE", "Job {} dispatched (type={})", jid, run_info.type);
  }
}

} // namespace server
} // namespace instserver
