#include "instrument-script-server/daemon/PluginRegistry.hpp"
#include <filesystem>
#include <instrument-log/inst_logging.h>

namespace instserver::plugin {

bool PluginRegistry::load_plugin(const std::string &protocol_type,
                                 const std::string &plugin_path) {
  std::lock_guard lock(mutex_);

  if (plugins_.count(protocol_type) != 0U) {
    LOG_WARN("PLUGIN_REGISTRY", "LOAD",
             "Plugin already loaded for protocol: %s", protocol_type.c_str());
    return false;
  }

  try {
    auto loader = std::make_unique<PluginLoader>(plugin_path);

    if (!loader->is_loaded()) {
      LOG_ERROR("PLUGIN_REGISTRY", "LOAD", "Failed to load plugin: %s",
                loader->get_error().c_str());
      return false;
    }

    auto metadata = loader->get_metadata();

    if (metadata.api_version != INSTRUMENT_PLUGIN_API_VERSION) {
      LOG_ERROR("PLUGIN_REGISTRY", "LOAD",
                "Plugin API version mismatch: %d vs %d", metadata.api_version,
                INSTRUMENT_PLUGIN_API_VERSION);
      return false;
    }

    LOG_INFO("PLUGIN_REGISTRY", "LOAD",
             "Loaded plugin: %s v%d for protocol: %s", metadata.name,
             metadata.version, protocol_type.c_str());

    plugins_[protocol_type] = std::move(loader);
    plugin_paths_[protocol_type] = plugin_path;

    return true;
  } catch (const std::exception &ex) {
    LOG_ERROR("PLUGIN_REGISTRY", "LOAD", "Exception loading plugin: %s",
              ex.what());
    return false;
  }
}

void PluginRegistry::load_builtin_plugins() {
  LOG_INFO("PLUGIN_REGISTRY", "BUILTIN", "Loading built-in plugins");

  // Define built-in plugin locations
  std::vector<std::pair<std::string, std::vector<std::string>>> builtins = {
      {"VISA",
       {"./plugins/visa/visa_plugin.so",
        "./build/plugins/visa/visa_plugin.so"}},
  };

  for (const auto &[protocol, paths] : builtins) {
    // Skip if already loaded
    if (has_plugin(protocol)) {
      LOG_DEBUG("PLUGIN_REGISTRY", "BUILTIN",
                "Protocol '%s' already has a plugin loaded", protocol.c_str());
      continue;
    }

    // Try each path until one succeeds
    bool loaded = false;
    for (const auto &path : paths) {
      if (std::filesystem::exists(path)) {
        LOG_INFO("PLUGIN_REGISTRY", "BUILTIN",
                 "Attempting to load built-in %s plugin from: %s",
                 protocol.c_str(), path.c_str());
        if (load_plugin(protocol, path)) {
          loaded = true;
          LOG_INFO("PLUGIN_REGISTRY", "BUILTIN",
                   "Successfully loaded built-in %s plugin", protocol.c_str());
          break;
        }
      }
    }

    if (!loaded) {
      LOG_WARN("PLUGIN_REGISTRY", "BUILTIN",
               "Built-in %s plugin not found or failed to load.  "
               "Instruments using this protocol will need to specify a custom "
               "plugin path.",
               protocol.c_str());
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
  std::unique_ptr<PluginLoader> loader;

  {
    std::lock_guard lock(mutex_);

    auto it = plugins_.find(protocol_type);
    if (it != plugins_.end()) {
      loader = std::move(it->second);
      plugins_.erase(it);
    }

    plugin_paths_.erase(protocol_type);
  }

  if (loader) {
    loader->shutdown();
  }

  LOG_INFO("PLUGIN_REGISTRY", "UNLOAD", "Unloaded plugin for protocol: %s",
           protocol_type.c_str());
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

std::filesystem::path
PluginRegistry::get_plugin_path(const std::string &protocol_type) const {
  std::lock_guard lock(mutex_);
  auto it = plugin_paths_.find(protocol_type);
  if (it == plugin_paths_.end()) {
    return "";
  }
  return {it->second};
}

void PluginRegistry::discover_plugins(
    const std::vector<std::string> &search_paths) {

  namespace fs = std::filesystem;

  LOG_INFO("PLUGIN_REGISTRY", "DISCOVER",
           "Discovering plugins in %d directories", search_paths.size());

  for (const auto &search_path : search_paths) {
    try {
      std::error_code ec;

      if (!fs::exists(search_path, ec) || !fs::is_directory(search_path, ec)) {
        LOG_WARN("PLUGIN_REGISTRY", "DISCOVER", "Invalid search path: %s",
                 search_path.c_str());
        continue;
      }

      for (fs::directory_iterator it(search_path, ec), end; it != end;
           it.increment(ec)) {

        if (ec) {
          LOG_WARN("PLUGIN_REGISTRY", "DISCOVER", "Iterator error in %s: %s",
                   search_path.c_str(), ec.message().c_str());
          break;
        }

        const fs::directory_entry &entry = *it;

        if (!entry.is_regular_file(ec)) {
          continue;
        }

        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

#ifdef _WIN32
        if (ext != ".dll") {
          continue;
        }
#else
        if (ext != ".so") {
          continue;
        }
#endif

        std::string plugin_path = entry.path().string();

        try {
          PluginLoader temp_loader(plugin_path);

          if (!temp_loader.is_loaded()) {
            continue;
          }
          auto metadata = temp_loader.get_metadata();
          std::string protocol = metadata.protocol_type;

          if (!has_plugin(protocol)) {
            load_plugin(protocol, plugin_path);
          }

        } catch (const std::exception &ex) {
          LOG_WARN("PLUGIN_REGISTRY", "DISCOVER",
                   "Failed to load plugin %s: %s", plugin_path.c_str(),
                   ex.what());
        }
      }

    } catch (const std::exception &ex) {
      LOG_WARN("PLUGIN_REGISTRY", "DISCOVER", "Exception scanning %s: %s",
               search_path.c_str(), ex.what());
    }
  }

  LOG_INFO("PLUGIN_REGISTRY", "DISCOVER",
           "Discovery complete. %d plugins loaded", plugins_.size());
}

void PluginRegistry::unload_all() {
  std::vector<std::unique_ptr<PluginLoader>> loaders;

  {
    std::lock_guard lock(mutex_);

    // Move all loaders out of the map
    for (auto &[protocol, loader] : plugins_) {
      if (loader) {
        loaders.push_back(std::move(loader));
      }
    }

    plugins_.clear();
    plugin_paths_.clear();
  }

  for (auto &loader : loaders) {
    if (loader) {
      loader->shutdown();
    }
  }

  LOG_INFO("PLUGIN_REGISTRY", "UNLOAD", "Unloaded all plugins");
}

} // namespace instserver::plugin
