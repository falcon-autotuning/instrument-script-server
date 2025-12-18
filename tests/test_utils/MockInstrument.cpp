#include "MockInstrument.hpp"
#include <cstring>
#include <memory>
#include <thread>

namespace instserver {
namespace test {

MockInstrument::MockInstrument(const std::string &name) : name_(name) {}

void MockInstrument::set_response(const std::string &verb,
                                  const std::string &response) {
  std::lock_guard lock(mutex_);
  responses_[verb] = response;
}

void MockInstrument::set_delay(const std::string &verb,
                               std::chrono::milliseconds delay) {
  std::lock_guard lock(mutex_);
  delays_[verb] = delay;
}

void MockInstrument::set_error(const std::string &verb,
                               const std::string &error) {
  std::lock_guard lock(mutex_);
  errors_[verb] = error;
}

std::vector<std::string> MockInstrument::get_command_history() const {
  std::lock_guard lock(mutex_);
  return command_history_;
}

size_t MockInstrument::command_count() const {
  std::lock_guard lock(mutex_);
  return command_history_.size();
}

void MockInstrument::clear_history() {
  std::lock_guard lock(mutex_);
  command_history_.clear();
}

PluginMetadata MockInstrument::get_metadata() {
  PluginMetadata meta = {};
  meta.api_version = INSTRUMENT_PLUGIN_API_VERSION;
  strncpy(meta.name, "Mock Instrument", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(meta.version, "1.0.0", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(meta.protocol_type, "Mock", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(meta.description, "Mock instrument for testing",
          PLUGIN_MAX_STRING_LEN - 1);
  return meta;
}

int32_t MockInstrument::initialize(const PluginConfig *config) {
  initialized_ = true;
  return 0;
}

int32_t MockInstrument::execute_command(const PluginCommand *command,
                                        PluginResponse *response) {
  std::lock_guard lock(mutex_);

  std::string verb = command->verb;
  command_history_.push_back(verb);

  // Fill response metadata
  strncpy(response->command_id, command->id, PLUGIN_MAX_STRING_LEN - 1);
  strncpy(response->instrument_name, command->instrument_name,
          PLUGIN_MAX_STRING_LEN - 1);

  // Simulate delay
  if (delays_.count(verb)) {
    std::this_thread::sleep_for(delays_[verb]);
  }

  // Check for error
  if (errors_.count(verb)) {
    response->success = false;
    strncpy(response->error_message, errors_[verb].c_str(),
            PLUGIN_MAX_STRING_LEN - 1);
    return -1;
  }

  // Return response
  if (responses_.count(verb)) {
    response->success = true;
    strncpy(response->text_response, responses_[verb].c_str(),
            PLUGIN_MAX_PAYLOAD - 1);

    // Try to parse as double
    try {
      double val = std::stod(responses_[verb]);
      response->return_value.type = PARAM_TYPE_DOUBLE;
      response->return_value.value.d_val = val;
    } catch (...) {
      // Not a number, leave as string
    }
  } else {
    response->success = true;
    strncpy(response->text_response, "OK", PLUGIN_MAX_PAYLOAD - 1);
  }

  return 0;
}

void MockInstrument::shutdown() { initialized_ = false; }

// MockInstrumentRegistry implementation

MockInstrumentRegistry &MockInstrumentRegistry::instance() {
  static MockInstrumentRegistry registry;
  return registry;
}

void MockInstrumentRegistry::register_instrument(
    const std::string &name, std::shared_ptr<MockInstrument> instrument) {
  std::lock_guard lock(mutex_);
  instruments_[name] = instrument;
}

std::shared_ptr<MockInstrument>
MockInstrumentRegistry::get_instrument(const std::string &name) {
  std::lock_guard lock(mutex_);
  auto it = instruments_.find(name);
  if (it != instruments_.end()) {
    return it->second;
  }
  return nullptr;
}

void MockInstrumentRegistry::clear() {
  std::lock_guard lock(mutex_);
  instruments_.clear();
}

} // namespace test
} // namespace instserver
