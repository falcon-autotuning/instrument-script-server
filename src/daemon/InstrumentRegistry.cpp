#include "instrument-script-server/daemon/InstrumentRegistry.hpp"
#include "instrument-script-server/daemon/ApiRefResolver.hpp"
#include "instrument-script-server/daemon/InstrumentWorkerProxy.hpp"
#include "instrument-script-server/daemon/PluginRegistry.hpp"
#include <instrument-log/inst_logging.h>
#include <nlohmann/json.hpp>
#include <yaml-cpp/yaml.h>

namespace instserver::daemon {

static nlohmann::json yaml_to_json(const YAML::Node &node) {
  if (node.IsNull()) {
    return nullptr;
  }
  if (node.IsScalar()) {
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

bool InstrumentRegistry::create_instrument(const std::string &config_path,
                                           const std::string &log_level) {
  LOG_INFO("REGISTRY", "CREATE", "Loading instrument from: %s",
           config_path.c_str());
  YAML::Node config_yaml;
  try {
    config_yaml = YAML::LoadFile(config_path);
  } catch (const std::exception &ex) {
    LOG_ERROR("REGISTRY", "CREATE", "Failed to load config: %s", ex.what());
    return false;
  }
  nlohmann::json config = yaml_to_json(config_yaml);

  std::string api_ref = config["api_ref"];

  std::string resolved_api_path;
  try {
    resolved_api_path = daemon::resolve_api_ref(api_ref, config_path);
  } catch (const std::exception &e) {
    LOG_ERROR("REGISTRY", "CREATE",
              "Failed to resolve api_ref '%s' (from config '%s'): %s",
              api_ref.c_str(), config_path.c_str(), e.what());
    return false;
  }
  YAML::Node api_yaml;
  try {
    api_yaml = YAML::LoadFile(resolved_api_path);
  } catch (const std::exception &ex) {
    LOG_ERROR("REGISTRY", "CREATE", "Failed to load api: %s", ex.what());
    return false;
  }
  nlohmann::json api_def = yaml_to_json(api_yaml);

  std::string name = config["name"];

  // Get protocol type
  std::string protocol_type = api_def["protocol"]["type"].get<std::string>();

  // Look up in plugin registry
  auto &plugin_registry = plugin::PluginRegistry::instance();
  auto plugin = plugin_registry.get_plugin_path(protocol_type);
  if (plugin.empty()) {
    LOG_ERROR("REGISTRY", "CREATE", "No plugin found for protocol: %s",
              protocol_type.c_str());
    return false;
  }
  bool exists = false;
  {
    std::lock_guard lock(mutex_);
    exists = instruments_.contains(name);
  }

  if (exists) {
    LOG_WARN("REGISTRY", "CREATE", "Instrument already exists: %s",
             name.c_str());
    return false;
  }

  LOG_INFO("REGISTRY", "CREATE",
           "Creating instrument '%s' with protocol '%s' using plugin:  %s",
           name.c_str(), protocol_type.c_str(), plugin.c_str());

  // Create worker proxy with JSON strings
  auto proxy = std::make_shared<InstrumentWorkerProxy>(
      name, plugin, std::filesystem::path(config_path), log_level);

  if (!proxy->start()) {
    LOG_ERROR("REGISTRY", "CREATE", "Failed to start worker for:  %s",
              name.c_str());
    std::lock_guard lock(mutex_);
    return false;
  }

  {
    std::lock_guard lock(mutex_);
    instruments_[name] = proxy;
  }

  LOG_INFO("REGISTRY", "CREATE", "Instrument '%s' created successfully",
           name.c_str());
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
    LOG_INFO("REGISTRY", "REMOVE", "Removed instrument: %s", name.c_str());
  }
}

void InstrumentRegistry::stop_all() {
  std::vector<std::shared_ptr<InstrumentWorkerProxy>> proxies;

  {
    std::lock_guard lock(mutex_);

    LOG_INFO("REGISTRY", "STOP_ALL", "Stopping %d instruments",
             instruments_.size());

    for (auto &[name, proxy] : instruments_) {
      if (proxy) {
        proxies.push_back(proxy);
      }
    }

    instruments_.clear();
  }

  for (auto &proxy : proxies) {
    try {
      proxy->stop();
    } catch (const std::exception &e) {
      LOG_ERROR("REGISTRY", "STOP_ALL", "Error stopping instrument: %s",
                e.what());
    }
  }
}

void InstrumentRegistry::start_all() {
  std::lock_guard lock(mutex_);
  LOG_INFO("REGISTRY", "START_ALL", "Starting %d instruments",
           instruments_.size());

  for (auto &[name, proxy] : instruments_) {
    if (proxy && !proxy->is_alive()) {
      try {
        proxy->start();
      } catch (const std::exception &e) {
        LOG_ERROR("REGISTRY", "START_ALL", "Error starting instrument %s: %s",
                  name.c_str(), e.what());
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

} // namespace instserver::daemon
