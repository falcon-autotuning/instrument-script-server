#include "instrument-server/server/InstrumentRegistry.hpp"
#include "instrument-server/Logger.hpp"
#include "instrument-server/plugin/PluginRegistry.hpp"
#include "instrument-server/server/InstrumentWorkerProxy.hpp"
#include <nlohmann/json.hpp>
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

    // Convert JSON objects to strings for worker
    std::string config_json = config.dump();
    std::string api_def_json = api_def.dump();

    return create_instrument_from_json(name, config_json, api_def_json);
  } catch (const std::exception &ex) {
    LOG_ERROR("REGISTRY", "CREATE", "Failed to load config: {}", ex.what());
    return false;
  }
}

bool InstrumentRegistry::create_instrument_from_json(
    const std::string &name, const std::string &config_json,
    const std::string &api_def_json) {
  std::lock_guard lock(mutex_);

  if (instruments_.count(name)) {
    LOG_WARN("REGISTRY", "CREATE", "Instrument already exists: {}", name);
    return false;
  }

  // Parse JSON strings
  nlohmann::json config = nlohmann::json::parse(config_json);
  nlohmann::json api_def = nlohmann::json::parse(api_def_json);

  // Store metadata for later lookup
  InstrumentMetadata metadata;
  metadata.name = name;
  metadata.config = config;
  metadata.api_def = api_def;
  metadata_[name] = metadata;

  // Get protocol type
  std::string protocol_type = api_def["protocol"]["type"];

  // Look up in plugin registry
  auto &plugin_registry = plugin::PluginRegistry::instance();
  std::string plugin_path = plugin_registry.get_plugin_path(protocol_type);

  if (plugin_path.empty()) {
    LOG_ERROR("REGISTRY", "CREATE", "No plugin found for protocol: {}",
              protocol_type);
    metadata_.erase(name);
    return false;
  }

  LOG_INFO("REGISTRY", "CREATE",
           "Creating instrument '{}' with protocol '{}' using plugin:  {}",
           name, protocol_type, plugin_path);

  // Create worker proxy with JSON strings
  auto proxy = std::make_shared<InstrumentWorkerProxy>(
      name, plugin_path, config_json, api_def_json, sync_coordinator_);

  if (!proxy->start()) {
    LOG_ERROR("REGISTRY", "CREATE", "Failed to start worker for:  {}", name);
    metadata_.erase(name);
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

std::optional<InstrumentMetadata>
InstrumentRegistry::get_instrument_metadata(const std::string &name) const {
  std::lock_guard lock(mutex_);
  auto it = metadata_.find(name);
  if (it == metadata_.end()) {
    return std::nullopt;
  }
  return it->second;
}

const nlohmann::json *
find_command_def(const std::map<std::string, InstrumentMetadata> &metadata,
                 const std::string &instrument_name, const std::string &verb) {
  auto meta_it = metadata.find(instrument_name);
  if (meta_it == metadata.end()) {
    LOG_WARN("REGISTRY", "API_LOOKUP", "No metadata found for instrument: {}",
             instrument_name);
    return nullptr;
  }
  const auto &api_def = meta_it->second.api_def;
  if (!api_def.contains("commands") || !api_def["commands"].is_object()) {
    LOG_WARN("REGISTRY", "API_LOOKUP",
             "No commands section in API definition for:  {}", instrument_name);
    return nullptr;
  }
  const auto &commands = api_def["commands"];
  if (!commands.contains(verb)) {
    LOG_WARN("REGISTRY", "API_LOOKUP",
             "Command '{}' not found in API definition for instrument '{}'",
             verb, instrument_name);
    return nullptr;
  }
  return &commands[verb];
}

bool InstrumentRegistry::command_expects_response(
    const std::string &instrument_name, const std::string &verb) const {
  std::lock_guard lock(mutex_);

  const nlohmann::json *cmd_def =
      find_command_def(metadata_, instrument_name, verb);
  if (!cmd_def) {
    return false;
  }

  if (cmd_def->contains("outputs") && (*cmd_def)["outputs"].is_array()) {
    const auto &outputs = (*cmd_def)["outputs"];
    return !outputs.empty();
  }
  return false;
}

std::optional<std::string>
InstrumentRegistry::get_response_type(const std::string &instrument_name,
                                      const std::string &verb) const {
  std::lock_guard lock(mutex_);

  auto meta_it = metadata_.find(instrument_name);
  if (meta_it == metadata_.end()) {
    return std::nullopt;
  }
  const auto &api_def = meta_it->second.api_def;

  const nlohmann::json *cmd_def =
      find_command_def(metadata_, instrument_name, verb);
  if (!cmd_def) {
    return std::nullopt;
  }

  if (!cmd_def->contains("outputs") || !(*cmd_def)["outputs"].is_array()) {
    return std::nullopt;
  }
  const auto &outputs = (*cmd_def)["outputs"];
  if (outputs.empty()) {
    return std::nullopt;
  }
  std::string output_name = outputs[0].get<std::string>();

  // Search in io section
  if (api_def.contains("io") && api_def["io"].is_array()) {
    for (const auto &io : api_def["io"]) {
      if (io.contains("name") && io["name"].get<std::string>() == output_name) {
        if (io.contains("type")) {
          return io["type"].get<std::string>();
        }
      }
    }
  }

  // Search in channel_groups' io_types
  if (api_def.contains("channel_groups") &&
      api_def["channel_groups"].is_array()) {
    for (const auto &group : api_def["channel_groups"]) {
      if (group.contains("io_types") && group["io_types"].is_array()) {
        for (const auto &io_type : group["io_types"]) {
          if (io_type.contains("suffix") &&
              io_type["suffix"].get<std::string>() == output_name) {
            if (io_type.contains("type")) {
              return io_type["type"].get<std::string>();
            }
          }
        }
      }
    }
  }

  LOG_WARN("REGISTRY", "API_LOOKUP",
           "Output '{}' not found in io or channel_groups for instrument '{}'",
           output_name, instrument_name);
  return std::nullopt;
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
    metadata_.erase(name);
    LOG_INFO("REGISTRY", "REMOVE", "Removed instrument: {}", name);
  }
}

void InstrumentRegistry::stop_all() {
  std::lock_guard lock(mutex_);
  LOG_INFO("REGISTRY", "STOP_ALL", "Stopping {} instruments",
           instruments_.size());

  std::vector<std::shared_ptr<InstrumentWorkerProxy>> proxies;
  for (auto &[name, proxy] : instruments_) {
    if (proxy) {
      proxies.push_back(proxy);
    }
  }

  for (auto &proxy : proxies) {
    try {
      proxy->stop();
    } catch (const std::exception &e) {
      LOG_ERROR("REGISTRY", "STOP_ALL", "Error stopping instrument: {}",
                e.what());
    }
  }

  instruments_.clear();
  metadata_.clear();
}

void InstrumentRegistry::start_all() {
  std::lock_guard lock(mutex_);
  LOG_INFO("REGISTRY", "START_ALL", "Starting {} instruments",
           instruments_.size());

  for (auto &[name, proxy] : instruments_) {
    if (proxy && !proxy->is_alive()) {
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
