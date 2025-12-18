#pragma once
#include "instrument-server/plugin/PluginInterface.h"
#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace instserver {
namespace test {

/// Mock instrument for testing
class MockInstrument {
public:
  MockInstrument(const std::string &name);

  void set_response(const std::string &verb, const std::string &response);
  void set_delay(const std::string &verb, std::chrono::milliseconds delay);
  void set_error(const std::string &verb, const std::string &error);

  std::vector<std::string> get_command_history() const;
  size_t command_count() const;
  void clear_history();

  // Plugin interface implementation
  static PluginMetadata get_metadata();
  int32_t initialize(const PluginConfig *config);
  int32_t execute_command(const PluginCommand *command,
                          PluginResponse *response);
  void shutdown();

private:
  std::string name_;
  mutable std::mutex mutex_;
  std::vector<std::string> command_history_;
  std::map<std::string, std::string> responses_;
  std::map<std::string, std::chrono::milliseconds> delays_;
  std::map<std::string, std::string> errors_;
  std::atomic<bool> initialized_{false};
};

// Global registry for mock instruments (for plugin interface)
class MockInstrumentRegistry {
public:
  static MockInstrumentRegistry &instance();

  void register_instrument(const std::string &name,
                           std::shared_ptr<MockInstrument> instrument);
  std::shared_ptr<MockInstrument> get_instrument(const std::string &name);
  void clear();

private:
  std::mutex mutex_;
  std::map<std::string, std::shared_ptr<MockInstrument>> instruments_;
};

} // namespace test
} // namespace instserver
