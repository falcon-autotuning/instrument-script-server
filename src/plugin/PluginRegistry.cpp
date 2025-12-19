#include "instrument-server/plugin/PluginRegistry.hpp"
#include "instrument-server/Logger.hpp"
#include <filesystem>

namespace instserver {
namespace plugin {

bool PluginRegistry::load_plugin(const std::string &protocol_type,
                                 const std::string &plugin_path) {
  std::lock_guard lock(mutex_);

  if (plugins_.count(protocol_type)) {
    LOG_WARN("PLUGIN_REGISTRY", "LOAD",
             "Plugin already loaded for protocol: {}", protocol_type);
    return false;
  }

  try {
    auto loader = std::make_unique<PluginLoader>(plugin_path);

    if (!loader->is_loaded()) {
      LOG_ERROR("PLUGIN_REGISTRY", "LOAD", "Failed to load plugin: {}",
                loader->get_error());
      return false;
    }

    auto metadata = loader->get_metadata();

    if (metadata.api_version != INSTRUMENT_PLUGIN_API_VERSION) {
      LOG_ERROR("PLUGIN_REGISTRY", "LOAD",
                "Plugin API version mismatch: {} vs {}", metadata.api_version,
                INSTRUMENT_PLUGIN_API_VERSION);
      return false;
    }

    LOG_INFO("PLUGIN_REGISTRY", "LOAD",
             "Loaded plugin: {} v{} for protocol: {}", metadata.name,
             metadata.version, protocol_type);

    plugins_[protocol_type] = std::move(loader);
    plugin_paths_[protocol_type] = plugin_path;

    return true;
  } catch (const std::exception &ex) {
    LOG_ERROR("PLUGIN_REGISTRY", "LOAD", "Exception loading plugin: {}",
              ex.what());
    return false;
  }
}

void PluginRegistry::load_builtin_plugins() {
  LOG_INFO("PLUGIN_REGISTRY", "BUILTIN", "Loading built-in plugins");

  // Define built-in plugin locations
  std::vector<std::pair<std::string, std::vector<std::string>>> builtins = {
      {"VISA",
       {"/usr/local/lib/instrument-plugins/visa_plugin.so",
        "/usr/lib/instrument-plugins/visa_plugin.so",
        "./plugins/visa/visa_plugin. so",
        "./build/plugins/visa/visa_plugin.so"}},
      // Future built-in plugins can be added here
      // {"Serial", {"/usr/local/lib/instrument-plugins/serial_plugin.so", ...}}
  };

  for (const auto &[protocol, paths] : builtins) {
    // Skip if already loaded
    if (has_plugin(protocol)) {
      LOG_DEBUG("PLUGIN_REGISTRY", "BUILTIN",
                "Protocol '{}' already has a plugin loaded", protocol);
      continue;
    }

    // Try each path until one succeeds
    bool loaded = false;
    for (const auto &path : paths) {
      if (std::filesystem::exists(path)) {
        LOG_INFO("PLUGIN_REGISTRY", "BUILTIN",
                 "Attempting to load built-in {} plugin from: {}", protocol,
                 path);
        if (load_plugin(protocol, path)) {
          loaded = true;
          LOG_INFO("PLUGIN_REGISTRY", "BUILTIN",
                   "Successfully loaded built-in {} plugin", protocol);
          break;
        }
      }
    }

    if (!loaded) {
      LOG_WARN("PLUGIN_REGISTRY", "BUILTIN",
               "Built-in {} plugin not found or failed to load.  "
               "Instruments using this protocol will need to specify a custom "
               "plugin path.",
               protocol);
    }
  }
}

PluginLoader *PluginRegistry::get_plugin(const std::string &protocol_type) {
  std::lock_guard lock(mutex_);
  auto it = plugins_.find(protocol_type);
  if (it == plugins_.end()) {
    return nullptr;
  }
  return it->second.get();
}

bool PluginRegistry::has_plugin(const std::string &protocol_type) const {
  std::lock_guard lock(mutex_);
  return plugins_.count(protocol_type) > 0;
}

void PluginRegistry::unload_plugin(const std::string &protocol_type) {
  std::lock_guard lock(mutex_);
  plugins_.erase(protocol_type);
  plugin_paths_.erase(protocol_type);
  LOG_INFO("PLUGIN_REGISTRY", "UNLOAD", "Unloaded plugin for protocol: {}",
           protocol_type);
}

std::vector<std::string> PluginRegistry::list_protocols() const {
  std::lock_guard lock(mutex_);
  std::vector<std::string> protocols;
  protocols.reserve(plugins_.size());
  for (const auto &[proto, _] : plugins_) {
    protocols.push_back(proto);
  }
  return protocols;
}

std::string
PluginRegistry::get_plugin_path(const std::string &protocol_type) const {
  std::lock_guard lock(mutex_);
  auto it = plugin_paths_.find(protocol_type);
  if (it == plugin_paths_.end()) {
    return "";
  }
  return it->second;
}

void PluginRegistry::discover_plugins(
    const std::vector<std::string> &search_paths) {
  namespace fs = std::filesystem;

  LOG_INFO("PLUGIN_REGISTRY", "DISCOVER",
           "Discovering plugins in {} directories", search_paths.size());

  for (const auto &search_path : search_paths) {
    if (!fs::exists(search_path) || !fs::is_directory(search_path)) {
      LOG_WARN("PLUGIN_REGISTRY", "DISCOVER", "Invalid search path: {}",
               search_path);
      continue;
    }

    for (const auto &entry : fs::directory_iterator(search_path)) {
      if (!entry.is_regular_file())
        continue;

      std::string filename = entry.path().filename().string();

      // Check for plugin library extension
#ifdef _WIN32
      if (filename.length() < 4 ||
          filename.compare(filename.length() - 4, 4, ".dll") != 0)
        continue;
#else
      if (filename.length() < 3 ||
          filename.compare(filename.length() - 3, 3, ".so") != 0)
        continue;
#endif

      // Try to load plugin
      std::string plugin_path = entry.path().string();

      try {
        PluginLoader temp_loader(plugin_path);
        if (temp_loader.is_loaded()) {
          auto metadata = temp_loader.get_metadata();
          std::string protocol = metadata.protocol_type;

          if (!has_plugin(protocol)) {
            load_plugin(protocol, plugin_path);
          }
        }
      } catch (const std::exception &ex) {
        LOG_WARN("PLUGIN_REGISTRY", "DISCOVER",
                 "Failed to discover plugin {}: {}", plugin_path, ex.what());
      }
    }
  }

  LOG_INFO("PLUGIN_REGISTRY", "DISCOVER",
           "Discovery complete. {} plugins loaded", plugins_.size());
}

} // namespace plugin
} // namespace instserver
