#pragma once

#include <nlohmann/json.hpp>

namespace instserver {
namespace server {

/// Unified command handlers usable by both the CLI and the HTTP RPC server.
///
/// Each handler accepts a JSON `params` object (mirrors CLI args) and fills
/// `out` with a JSON response. Handlers return an integer exit code (0 success,
/// non-zero failure) for compatibility with the CLI.
int handle_daemon(const nlohmann::json &params, nlohmann::json &out);
int handle_start(const nlohmann::json &params, nlohmann::json &out);
int handle_stop(const nlohmann::json &params, nlohmann::json &out);
int handle_status(const nlohmann::json &params, nlohmann::json &out);
int handle_list(const nlohmann::json &params, nlohmann::json &out);
int handle_measure(const nlohmann::json &params, nlohmann::json &out);
int handle_test(const nlohmann::json &params, nlohmann::json &out);
int handle_discover(const nlohmann::json &params, nlohmann::json &out);
int handle_plugins(const nlohmann::json &params, nlohmann::json &out);
int handle_submit_job(const nlohmann::json &params, nlohmann::json &out);
int handle_submit_measure(const nlohmann::json &params, nlohmann::json &out);
int handle_job_status(const nlohmann::json &params, nlohmann::json &out);
int handle_job_result(const nlohmann::json &params, nlohmann::json &out);
int handle_job_list(const nlohmann::json &params, nlohmann::json &out);
int handle_job_cancel(const nlohmann::json &params, nlohmann::json &out);

} // namespace server
} // namespace instserver
