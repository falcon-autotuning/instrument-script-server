#pragma once
#include "instrument-server/SerializedCommand.hpp"
#include "instrument-server/server/InstrumentRegistry.hpp"
#include "instrument-server/server/SyncCoordinator.hpp"
#include <optional>
#include <sol/sol.hpp>
#include <unordered_map>
#include <vector>

namespace instserver {

/// InstrumentTarget (from Lua types)
struct InstrumentTarget {
  std::string id;
  std::optional<int> channel;

  std::string serialize() const {
    if (channel) {
      return id + ":" + std::to_string(*channel);
    }
    return id;
  }
};

/// Domain (from Lua types)
struct Domain {
  double min;
  double max;
};

/// Base runtime context with common operations
class RuntimeContextBase {
public:
  explicit RuntimeContextBase(InstrumentRegistry &registry,
                              SyncCoordinator &sync_coordinator);
  virtual ~RuntimeContextBase() = default;

  /// Call an instrument command
  sol::object call(const std::string &func_name, sol::variadic_args args,
                   sol::this_state s);

  /// Execute block in parallel with synchronization
  void parallel(sol::function block);

  /// Log message
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
               const std::unordered_map<std::string, ParamValue> &params);

  // Execute buffered parallel commands with sync
  void execute_parallel_buffer();
};

/// RuntimeContext_DCGetSet
class RuntimeContext_DCGetSet : public RuntimeContextBase {
public:
  explicit RuntimeContext_DCGetSet(InstrumentRegistry &registry,
                                   SyncCoordinator &sync_coordinator);

  std::vector<InstrumentTarget> getters;
  std::vector<InstrumentTarget> setters;
  std::unordered_map<std::string, double> setVoltages;
  double sampleRate{1000.0};
  int numPoints{100};
};

/// RuntimeContext_1DWaveform
class RuntimeContext_1DWaveform : public RuntimeContextBase {
public:
  explicit RuntimeContext_1DWaveform(InstrumentRegistry &registry,
                                     SyncCoordinator &sync_coordinator);

  std::vector<InstrumentTarget> setters;
  std::vector<InstrumentTarget> bufferedGetters;
  std::vector<InstrumentTarget> bufferedSetters;
  std::unordered_map<std::string, Domain> setVoltageDomains;
  double sampleRate{1e6};
  int numPoints{1000};
  int numSteps{10};
};

/// RuntimeContext_2DWaveform
class RuntimeContext_2DWaveform : public RuntimeContextBase {
public:
  explicit RuntimeContext_2DWaveform(InstrumentRegistry &registry,
                                     SyncCoordinator &sync_coordinator);

  std::vector<InstrumentTarget> setters;
  std::vector<InstrumentTarget> bufferedGetters;
  std::vector<InstrumentTarget> bufferedXSetters;
  std::vector<InstrumentTarget> bufferedYSetters;
  std::unordered_map<std::string, Domain> setXVoltageDomains;
  std::unordered_map<std::string, Domain> setYVoltageDomains;
  double sampleRate{1e6};
  int numPoints{1000};
  int numXSteps{10};
  int numYSteps{10};
};

/// Bind all runtime contexts to Lua
void bind_runtime_contexts(sol::state &lua, InstrumentRegistry &registry,
                           SyncCoordinator &sync_coordinator);

} // namespace instserver
