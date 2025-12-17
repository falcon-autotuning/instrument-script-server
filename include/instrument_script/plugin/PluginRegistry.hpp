#pragma once
#include "PluginLoader.hpp"
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>

namespace instrument_script {
namespace plugin {

/// Registry for managing loaded plugins
class PluginRegistry {
public:
  static PluginRegistry &instance() {
    static PluginRegistry registry;
    return registry;
  }

  /// Load plugin from path and register it
  bool load_plugin(const std::string &protocol_type,
                   const std::string &plugin_path);

  /// Get plugin loader for protocol type
  PluginLoader *get_plugin(const std::string &protocol_type);

  /// Check if plugin exists for protocol
  bool has_plugin(const std::string &protocol_type) const;

  /// Unload plugin
  void unload_plugin(const std::string &protocol_type);

  /// List all registered protocols
  std::vector<std::string> list_protocols() const;

  /// Discover plugins in standard directories
  void discover_plugins(const std::vector<std::string> &search_paths);

  /// Get plugin path for protocol (if registered)
  std::string get_plugin_path(const std::string &protocol_type) const;

private:
  PluginRegistry() = default;

  std::unordered_map<std::string, std::unique_ptr<PluginLoader>> plugins_;
  std::unordered_map<std::string, std::string> plugin_paths_;
  mutable std::mutex mutex_;
};

} // namespace plugin
} // namespace instrument_script
