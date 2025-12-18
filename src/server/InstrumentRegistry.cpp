#include "instrument-server/server/InstrumentRegistry.hpp"
#include "instrument-server/Logger.hpp"
#include "instrument-server/plugin/PluginRegistry.hpp"
#include <yaml-cpp/yaml.h>

namespace instserver {

static nlohmann::json yaml_to_json(const YAML::Node &node) {
  if (node.IsNull()) {
    return nullptr;
  } else if (node.IsScalar()) {
    try {
      return node.as<int64_t>();
    } catch (...) {
      try {
        return node.as<double>();
      } catch (...) {
        try {
          return node.as<bool>();
        } catch (...) {
          return node.as<std::string>();
        }
      }
    }
  } else if (node.IsSequence()) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto &item : node) {
      arr.push_back(yaml_to_json(item));
    }
    return arr;
  } else if (node.IsMap()) {
    nlohmann::json obj = nlohmann::json::object();
    for (const auto &kv : node) {
      obj[kv.first.as<std::string>()] = yaml_to_json(kv.second);
    }
    return obj;
  }
  return nullptr;
}

bool InstrumentRegistry::create_instrument(const std::string &config_path) {
  LOG_INFO("REGISTRY", "CREATE", "Loading instrument from: {}", config_path);

  try {
    YAML::Node config_yaml = YAML::LoadFile(config_path);
    nlohmann::json config = yaml_to_json(config_yaml);

    std::string api_ref = config["api_ref"];

    YAML::Node api_yaml = YAML::LoadFile(api_ref);
    nlohmann::json api_def = yaml_to_json(api_yaml);

    std::string name = config["name"];
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

void InstrumentRegistry::stop_all() {
  std::lock_guard lock(mutex_);
  LOG_INFO("REGISTRY", "STOP_ALL", "Stopping {} instruments",
           instruments_.size());

  // Create a copy of the map to avoid iterator invalidation
  std::vector<std::shared_ptr<InstrumentWorkerProxy>> proxies;
  for (auto &[name, proxy] : instruments_) {
    if (proxy) { // Null check
      proxies.push_back(proxy);
    }
  }

  // Stop all proxies
  for (auto &proxy : proxies) {
    try {
      proxy->stop();
    } catch (const std::exception &e) {
      LOG_ERROR("REGISTRY", "STOP_ALL", "Error stopping instrument: {}",
                e.what());
    }
  }
  instruments_.clear();
}

void InstrumentRegistry::start_all() {
  std::lock_guard lock(mutex_);
  LOG_INFO("REGISTRY", "START_ALL", "Starting {} instruments",
           instruments_.size());

  for (auto &[name, proxy] : instruments_) {
    if (proxy && !proxy->is_alive()) { // Null check
      try {
        proxy->start();
      } catch (const std::exception &e) {
        LOG_ERROR("REGISTRY", "START_ALL", "Error starting instrument {}: {}",
                  name, e.what());
      }
    }
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
