#pragma once
#include "instrument-server/export.h"

#include "PluginInterface.h"

#include <string>

#ifdef _WIN32
#include "instrument-server/compat/WinSock.hpp"
#include <windows.h>
using LibraryHandle = HMODULE;
#else
#include <dlfcn.h>
using LibraryHandle = void *;
#endif

namespace instserver {
namespace plugin {

/// RAII wrapper for dynamically loaded plugin
class INSTRUMENT_SERVER_API PluginLoader {
public:
  /// Load plugin from shared library path
  explicit PluginLoader(const std::string &plugin_path);

  /// Destructor unloads library
  ~PluginLoader();

  // Disable copy, allow move
  PluginLoader(const PluginLoader &) = delete;
  PluginLoader &operator=(const PluginLoader &) = delete;
  PluginLoader(PluginLoader &&other) noexcept;
  PluginLoader &operator=(PluginLoader &&other) noexcept;

  /// Check if plugin loaded successfully
  bool is_loaded() const { return handle_ != nullptr; }

  /// Get plugin metadata
  PluginMetadata get_metadata() const;

  /// Initialize plugin
  int32_t initialize(const PluginConfig &config);

  /// Execute command
  int32_t execute_command(const PluginCommand &command,
                          PluginResponse &response);

  /// Shutdown plugin
  void shutdown();

  /// Get last error message
  const std::string &get_error() const { return error_message_; }

private:
  LibraryHandle handle_{nullptr};
  std::string plugin_path_;
  std::string error_message_;

  // Function pointers to plugin functions
  decltype(&plugin_get_metadata) fn_get_metadata_{nullptr};
  decltype(&plugin_initialize) fn_initialize_{nullptr};
  decltype(&plugin_execute_command) fn_execute_command_{nullptr};
  decltype(&plugin_shutdown) fn_shutdown_{nullptr};

  void load_symbols();
  void unload();
};

} // namespace plugin
} // namespace instserver
