#pragma once
#include "instrument-script-server/ErrorCodes.hpp"
#include "instrument-script-server/export.h"
#include <string>

#include <instrument-plugin.h>

#ifdef _WIN32
#include "instrument-script-server/compat/WinSock.hpp"
#include <windows.h>
using LibraryHandle = HMODULE;
#else
#include <dlfcn.h>
using LibraryHandle = void *;
#endif

namespace instserver::plugin {

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
  [[nodiscard]] bool is_loaded() const { return handle_ != nullptr; }

  /// Get plugin metadata
  [[nodiscard]] PluginMetadata get_metadata() const;

  /// Initialize plugin
  ErrorCode initialize(const PluginConfig *config);

  /// Execute command
  ErrorCode execute_command(const PluginCommand *command,
                            PluginResponse *response);

  /// Shutdown plugin
  void shutdown();

  /// Get last error message
  [[nodiscard]] const std::string &get_error() const { return error_message_; }

private:
  LibraryHandle handle_{nullptr};
  std::string plugin_path_;
  std::string error_message_;
  bool shutdown_called_ = false;

  // Function pointers to plugin functions
  decltype(&plugin_get_metadata) fn_get_metadata_{nullptr};
  decltype(&plugin_initialize) fn_initialize_{nullptr};
  decltype(&plugin_execute_command) fn_execute_command_{nullptr};
  decltype(&plugin_shutdown) fn_shutdown_{nullptr};

  void load_symbols();
  void unload();
};

} // namespace instserver::plugin
