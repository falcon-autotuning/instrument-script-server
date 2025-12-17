#pragma once
#include "InstrumentWorkerProxy.hpp"
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>

namespace instserver {

/// Global registry of instrument worker proxies
class InstrumentRegistry {
public:
  static InstrumentRegistry &instance() {
    static InstrumentRegistry registry;
    return registry;
  }

  /// Create instrument from config file
  bool create_instrument(const std::string &config_path);

  /// Create instrument from JSON
  bool create_instrument_from_json(const std::string &name,
                                   const nlohmann::json &config,
                                   const nlohmann::json &api_def);

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

  std::unordered_map<std::string, std::shared_ptr<InstrumentWorkerProxy>>
      instruments_;
  mutable std::mutex mutex_;
};

} // namespace instserver
