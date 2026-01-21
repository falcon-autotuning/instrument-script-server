#pragma once
#include "instrument-server/export.h"

#include "instrument-server/SerializedCommand.hpp"
#include "instrument-server/server/InstrumentRegistry.hpp"
#include "instrument-server/server/SyncCoordinator.hpp"

#include <future>
#include <memory>
#include <set>
#include <sol/sol.hpp>
#include <unordered_map>
#include <vector>

namespace instserver {

/// Lua-accessible handle for array data buffers
/// Provides methods for array math operations
class INSTRUMENT_SERVER_API BufferHandle {
public:
  explicit BufferHandle(const std::string &buffer_id, uint64_t element_count,
                        const std::string &data_type);

  /// Get the buffer ID
  const std::string &id() const { return buffer_id_; }

  /// Get number of elements
  uint64_t size() const { return element_count_; }

  /// Get data type
  const std::string &type() const { return data_type_; }

  /// Add offset to all elements (array + scalar)
  bool add_offset(double offset);

  /// Multiply all elements by gain (array * scalar)
  bool multiply_gain(double gain);

private:
  std::string buffer_id_;
  uint64_t element_count_;
  std::string data_type_;
};

/// Lua-accessible measurement response object
/// Wraps scalar values with metadata and provides math operations
class INSTRUMENT_SERVER_API MeasurementResponse {
public:
  MeasurementResponse(const std::string &instrument, const std::string &verb,
                      double value_double);
  MeasurementResponse(const std::string &instrument, const std::string &verb,
                      int64_t value_int);
  MeasurementResponse(const std::string &instrument, const std::string &verb,
                      const std::string &value_str);
  MeasurementResponse(const std::string &instrument, const std::string &verb,
                      bool value_bool);
  MeasurementResponse(const std::string &instrument, const std::string &verb,
                      std::shared_ptr<BufferHandle> buffer);

  /// Get the instrument name
  const std::string &instrument() const { return instrument_; }

  /// Get the verb/command
  const std::string &verb() const { return verb_; }

  /// Get the return type
  const std::string &type() const { return type_; }

  /// Get the value (for scalars)
  sol::object value(sol::this_state s) const;

  /// Get the buffer (for arrays)
  std::shared_ptr<BufferHandle> buffer() const { return buffer_; }

  /// Add offset to numeric value (for scalars)
  std::shared_ptr<MeasurementResponse> add_offset(double offset) const;

  /// Multiply numeric value by gain (for scalars)
  std::shared_ptr<MeasurementResponse> multiply_gain(double gain) const;

private:
  std::string instrument_;
  std::string verb_;
  std::string type_;  // "float", "integer", "string", "boolean", "buffer"
  
  // Value storage (only one is used)
  double value_double_{0.0};
  int64_t value_int_{0};
  std::string value_str_;
  bool value_bool_{false};
  std::shared_ptr<BufferHandle> buffer_;
};

/// Result of a single context:call() operation
struct INSTRUMENT_SERVER_API CallResult {
  std::string command_id;
  std::string instrument_name;
  std::string verb;
  std::unordered_map<std::string, ParamValue> params;
  std::chrono::steady_clock::time_point executed_at;

  // Either a direct return value...
  std::optional<ParamValue> return_value;
  std::string return_type;

  // ...or a reference to large data buffer
  bool has_large_data{false};
  std::string buffer_id;
  uint64_t element_count{0};
  std::string data_type;

  // Execution status / error
  bool success{false};
  std::string error_message;
};

/// Generic runtime context for Lua scripts
/// Provides basic instrument control primitives:
/// - call(): Execute instrument commands (enqueue-first if enabled)
/// - parallel(): Synchronized parallel execution (dispatch-only; parsing blocks
/// only until dispatch)
/// - log(): Logging from scripts
class INSTRUMENT_SERVER_API RuntimeContext {
public:
  explicit RuntimeContext(InstrumentRegistry &registry,
                          SyncCoordinator &sync_coordinator,
                          bool enqueue_mode = false);
  virtual ~RuntimeContext() = default;

  /// Call an instrument command
  /// Usage: context:call("InstrumentID.CommandVerb", arg1, arg2, ...)
  /// Usage: context:call("InstrumentID:Channel.CommandVerb", value)
  sol::object call(const std::string &func_name, sol::variadic_args args,
                   sol::this_state s);

  /// Execute block in parallel with synchronization
  /// Usage: context:parallel(function() ... end)
  /// Note: parallel blocks dispatch commands with a shared sync token. The
  /// block returns after commands are dispatched (parsing resumes). Actual
  /// execution starts only when SYNC_CONTINUE is sent for that token.
  void parallel(sol::function block);

  /// Log message from script
  /// Usage: context:log("message")
  void log(const std::string &msg);

  /// Report an error from the script
  /// Usage: context:error("error message")
  /// This sets the error state and can be used to signal measurement failures
  void error(const std::string &msg);

  /// Check if an error has been set
  bool has_error() const { return has_error_; }

  /// Get the error message if one has been set
  const std::string &get_error() const { return error_message_; }

  /// Get collected results (filled after process_tokens_and_wait)
  const std::vector<CallResult> &get_results() const {
    return collected_results_;
  }

  /// Clear collected results
  void clear_results() { collected_results_.clear(); }

  /// After enqueueing (enqueue_mode), release tokens in order and wait for
  /// associated command futures to complete. This sends SYNC_CONTINUE in token
  /// order and blocks until completion. Intended for monitor thread use.
  void process_tokens_and_wait();

  /// Backwards-compatible alias used by JobManager monitor
  void wait_for_all_enqueued() { process_tokens_and_wait(); }

  /// Serialize collected results to JSON (for job result reporting)
  nlohmann::json collect_results_json() const;

protected:
  InstrumentRegistry &registry_;
  SyncCoordinator &sync_coordinator_;

  // Parallel execution state (used while parsing)
  bool in_parallel_block_{false};
  std::vector<SerializedCommand> parallel_buffer_;
  std::atomic<uint64_t> next_sync_token_{1};

  // Collected results from all call() operations
  std::vector<CallResult> collected_results_;

  // Error state tracking (for context:error())
  bool has_error_{false};
  std::string error_message_;

  // enqueue mode: if true, call() enqueues (worker->execute) and returns
  // immediately (collecting futures to wait on later). If false, call()
  // performs execute_sync and returns the response to Lua.
  bool enqueue_mode_{false};

  // Per-token data structures for ordered release and waiting
  // Order of tokens as created during parsing
  std::vector<uint64_t> token_order_;
  // token -> set of instruments participating (used to send SYNC_CONTINUE)
  std::unordered_map<uint64_t, std::set<std::string>> token_instruments_;
  // token -> vector of futures for commands tagged with that token
  std::unordered_map<uint64_t, std::vector<std::future<CommandResponse>>>
      token_futures_;
  // token -> vector of indices in collected_results_ corresponding to those
  // futures
  std::unordered_map<uint64_t, std::vector<size_t>> token_result_indices_;

  // Helper to send command to instrument (synchronous path)
  CommandResponse
  send_command(const std::string &instrument_id, const std::string &verb,
               const std::unordered_map<std::string, ParamValue> &params,
               bool expects_response);

  // Execute buffered parallel commands with sync (used only when not
  // enqueue_mode)
  void execute_parallel_buffer();
};

/// Bind runtime context to Lua and return the created context instance.
/// If enqueue_mode is true, the context will enqueue commands (non-blocking)
/// and allow callers to release tokens & wait on them later via
/// process_tokens_and_wait().
INSTRUMENT_SERVER_API std::shared_ptr<RuntimeContext>
bind_runtime_context(sol::state &lua, InstrumentRegistry &registry,
                     SyncCoordinator &sync_coordinator,
                     bool enqueue_mode = false);

} // namespace instserver
