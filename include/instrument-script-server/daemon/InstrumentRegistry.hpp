#pragma once

#include "instrument-script-server/daemon/InstrumentWorkerProxy.hpp"

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace instserver::daemon {

class InstrumentRegistry {
public:
  static InstrumentRegistry &instance() {
    static InstrumentRegistry registry;
    return registry;
  }

  /// Create instrument from config file
  bool create_instrument(const std::string &config_path,
                         const std::string &log_level = "info");

  /// Get instrument proxy
  std::shared_ptr<InstrumentWorkerProxy>
  get_instrument(const std::string &name);

  /// Check if instrument exists
  bool has_instrument(const std::string &name) const;

  /// Remove instrument (stops worker)
  void remove_instrument(const std::string &name);

  /// Start all instruments
  void start_all();

  /// Stop all instruments
  void stop_all();

  /// List all instruments
  std::vector<std::string> list_instruments() const;

private:
  InstrumentRegistry() = default;
  ~InstrumentRegistry() { stop_all(); }

  InstrumentRegistry(const InstrumentRegistry &) = delete;
  InstrumentRegistry &operator=(const InstrumentRegistry &) = delete;
  const Command *find_command(const std::string &instrument_name,
                              const std::string &verb) const;

  mutable std::mutex mutex_;
  std::map<std::string, std::shared_ptr<InstrumentWorkerProxy>> instruments_;
};

} // namespace instserver::daemon
