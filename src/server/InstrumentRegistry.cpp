#include "instrument-server/server/InstrumentRegistry.hpp"
#include "instrument-server/Logger.hpp"
#include "instrument-server/plugin/PluginRegistry.hpp"
#include <yaml-cpp/yaml.h>

namespace instserver {

bool InstrumentRegistry::create_instrument(const std::string &config_path) {
  LOG_INFO("REGISTRY", "CREATE", "Loading instrument from: {}", config_path);

  try {
    YAML::Node config_yaml = YAML::LoadFile(config_path);
    nlohmann::json config = nlohmann::json::parse(YAML::Dump(config_yaml));

    std::string name = config["name"];
    std::string api_ref = config["api_ref"];

    YAML::Node api_yaml = YAML::LoadFile(api_ref);
    nlohmann::json api_def = nlohmann::json::parse(YAML::Dump(api_yaml));

    return create_instrument_from_json(name, config, api_def);
  } catch (const std::exception &ex) {
    LOG_ERROR("REGISTRY", "CREATE", "Failed to load config: {}", ex.what());
    return false;
  }
}

bool InstrumentRegistry::create_instrument_from_json(
    const std::string &name, const nlohmann::json &config,
    const nlohmann::json &api_def) {
  std::lock_guard lock(mutex_);

  if (instruments_.count(name)) {
    LOG_WARN("REGISTRY", "CREATE", "Instrument already exists: {}", name);
    return false;
  }

  // Get protocol type
  std::string protocol_type = api_def["protocol"]["type"];

  // Get plugin path (inline in config or from registry)
  std::string plugin_path;
  if (config["connection"].contains("plugin")) {
    plugin_path = config["connection"]["plugin"];
  } else {
    // Look up in plugin registry
    auto &plugin_registry = plugin::PluginRegistry::instance();
    plugin_path = plugin_registry.get_plugin_path(protocol_type);

    if (plugin_path.empty()) {
      LOG_ERROR("REGISTRY", "CREATE", "No plugin found for protocol: {}",
                protocol_type);
      return false;
    }
  }

  LOG_INFO("REGISTRY", "CREATE",
           "Creating instrument '{}' with protocol '{}' using plugin:  {}",
           name, protocol_type, plugin_path);

  // Create worker proxy
  auto proxy = std::make_shared<InstrumentWorkerProxy>(name, plugin_path,
                                                       config, api_def);

  if (!proxy->start()) {
    LOG_ERROR("REGISTRY", "CREATE", "Failed to start worker for:  {}", name);
    return false;
  }

  instruments_[name] = proxy;

  LOG_INFO("REGISTRY", "CREATE", "Instrument '{}' created successfully", name);
  return true;
}

std::shared_ptr<InstrumentWorkerProxy>
InstrumentRegistry::get_instrument(const std::string &name) {
  std::lock_guard lock(mutex_);
  auto it = instruments_.find(name);
  if (it == instruments_.end()) {
    return nullptr;
  }
  return it->second;
}

bool InstrumentRegistry::has_instrument(const std::string &name) const {
  std::lock_guard lock(mutex_);
  return instruments_.count(name) > 0;
}

void InstrumentRegistry::remove_instrument(const std::string &name) {
  std::lock_guard lock(mutex_);
  auto it = instruments_.find(name);
  if (it != instruments_.end()) {
    it->second->stop();
    instruments_.erase(it);
    LOG_INFO("REGISTRY", "REMOVE", "Removed instrument: {}", name);
  }
}

void InstrumentRegistry::start_all() {
  std::lock_guard lock(mutex_);
  LOG_INFO("REGISTRY", "START_ALL", "Starting {} instruments",
           instruments_.size());
  for (auto &[name, proxy] : instruments_) {
    if (!proxy->is_alive()) {
      proxy->start();
    }
  }
}

void InstrumentRegistry::stop_all() {
  std::lock_guard lock(mutex_);
  LOG_INFO("REGISTRY", "STOP_ALL", "Stopping {} instruments",
           instruments_.size());
  for (auto &[name, proxy] : instruments_) {
    proxy->stop();
  }
}

std::vector<std::string> InstrumentRegistry::list_instruments() const {
  std::lock_guard lock(mutex_);
  std::vector<std::string> names;
  names.reserve(instruments_.size());
  for (const auto &[name, _] : instruments_) {
    names.push_back(name);
  }
  return names;
}

} // namespace instserver
