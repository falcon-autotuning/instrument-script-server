#pragma once
#include "instrument-server/SerializedCommand.hpp"
#include "instrument-server/server/InstrumentRegistry.hpp"
#include "instrument-server/server/SyncCoordinator.hpp"
#include <sol/sol.hpp>
#include <unordered_map>
#include <vector>

namespace instserver {

/// Generic runtime context for Lua scripts
/// Provides basic instrument control primitives:
/// - call(): Execute instrument commands
/// - parallel(): Synchronized parallel execution
/// - log(): Logging from scripts
class RuntimeContext {
public:
  explicit RuntimeContext(InstrumentRegistry &registry,
                          SyncCoordinator &sync_coordinator);
  virtual ~RuntimeContext() = default;

  /// Call an instrument command
  /// Usage: context:call("InstrumentID.CommandVerb", arg1, arg2, ...)
  /// Usage: context:call("InstrumentID:Channel.CommandVerb", value)
  sol::object call(const std::string &func_name, sol::variadic_args args,
                   sol::this_state s);

  /// Execute block in parallel with synchronization
  /// Usage: context:parallel(function() ... end)
  void parallel(sol::function block);

  /// Log message from script
  /// Usage: context:log("message")
  void log(const std::string &msg);

protected:
  InstrumentRegistry &registry_;
  SyncCoordinator &sync_coordinator_;

  // Parallel execution state
  bool in_parallel_block_{false};
  std::vector<SerializedCommand> parallel_buffer_;
  std::atomic<uint64_t> next_sync_token_{1};

  // Helper to send command to instrument
  CommandResponse
  send_command(const std::string &instrument_id, const std::string &verb,
               const std::unordered_map<std::string, ParamValue> &params,
               bool expects_response);

  // Execute buffered parallel commands with sync
  void execute_parallel_buffer();
};

/// Bind runtime context to Lua
void bind_runtime_context(sol::state &lua, InstrumentRegistry &registry,
                          SyncCoordinator &sync_coordinator);

} // namespace instserver
