#pragma once
#include "instrument-server/export.h"

#include "instrument-server/server/InstrumentWorkerProxy.hpp"
#include "instrument-server/server/SyncCoordinator.hpp"

#include <map>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace instserver {

// Instrument metadata stored by registry
struct INSTRUMENT_SERVER_API InstrumentMetadata {
  std::string name;
  nlohmann::json config;  // Full instrument config
  nlohmann::json api_def; // Full API definition
};

class INSTRUMENT_SERVER_API InstrumentRegistry {
public:
  static InstrumentRegistry &instance() {
    static InstrumentRegistry registry;
    return registry;
  }

  /// Create instrument from config file
  bool create_instrument(const std::string &config_path);

  /// Create instrument from JSON strings
  bool create_instrument_from_json(const std::string &name,
                                   const std::string &config_json,
                                   const std::string &api_def_json);

  /// Get instrument proxy
  std::shared_ptr<InstrumentWorkerProxy>
  get_instrument(const std::string &name);

  /// Get instrument metadata (config + API definition)
  std::optional<InstrumentMetadata>
  get_instrument_metadata(const std::string &name) const;

  /// Check if command expects response based on API definition
  bool command_expects_response(const std::string &instrument_name,
                                const std::string &verb) const;

  /// Get expected response type for command
  std::optional<std::string>
  get_response_type(const std::string &instrument_name,
                    const std::string &verb) const;

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

  mutable std::mutex mutex_;
  std::map<std::string, std::shared_ptr<InstrumentWorkerProxy>> instruments_;
  std::map<std::string, InstrumentMetadata> metadata_; // NEW: Store metadata
  SyncCoordinator sync_coordinator_;
};

} // namespace instserver
