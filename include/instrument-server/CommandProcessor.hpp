#pragma once
#include "SerializedCommand.hpp"
#include <memory>

namespace instserver {

/// CommandProcessor:  converts SerializedCommand -> transport-level operations
/// This is the unified interface for VISA and native APIs
class CommandProcessor {
public:
  virtual ~CommandProcessor() = default;

  /// Execute a serialized command and return response
  virtual CommandResponse execute(const SerializedCommand &cmd) = 0;

  /// Initialize/connect to instrument
  virtual bool initialize() = 0;

  /// Shutdown/disconnect
  virtual void shutdown() = 0;

  /// Get processor type for logging
  virtual std::string processor_type() const = 0;

  /// Get connection info
  virtual std::string connection_info() const = 0;
};

using CommandProcessorPtr = std::shared_ptr<CommandProcessor>;

} // namespace instserver
