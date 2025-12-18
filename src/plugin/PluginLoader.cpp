#include "instrument-server/plugin/PluginLoader.hpp"
#include "instrument-server/Logger.hpp"
#include <csignal>
#include <stdexcept>

namespace instserver {
namespace plugin {

#ifdef _WIN32
#define LOAD_LIBRARY(path) LoadLibraryA(path)
#define GET_SYMBOL(handle, name) GetProcAddress(handle, name)
#define CLOSE_LIBRARY(handle) FreeLibrary(handle)
#define LIBRARY_ERROR() "Windows LoadLibrary error"
#else
#define LOAD_LIBRARY(path) dlopen(path, RTLD_LAZY)
#define GET_SYMBOL(handle, name) dlsym(handle, name)
#define CLOSE_LIBRARY(handle) dlclose(handle)
#define LIBRARY_ERROR() dlerror()
#endif

PluginLoader::PluginLoader(const std::string &plugin_path)
    : plugin_path_(plugin_path) {

  LOG_INFO("PLUGIN", "LOAD", "Loading plugin: {}", plugin_path);

  handle_ = LOAD_LIBRARY(plugin_path.c_str());

  if (!handle_) {
    error_message_ = std::string("Failed to load library: ") + LIBRARY_ERROR();
    LOG_ERROR("PLUGIN", "LOAD", "{}", error_message_);
    throw std::runtime_error(error_message_);
  }

  load_symbols();

  if (!fn_get_metadata_ || !fn_initialize_ || !fn_execute_command_ ||
      !fn_shutdown_) {
    error_message_ = "Failed to load required plugin symbols";
    LOG_ERROR("PLUGIN", "LOAD", "{}", error_message_);
    unload();
    throw std::runtime_error(error_message_);
  }

  LOG_INFO("PLUGIN", "LOAD", "Plugin loaded successfully:  {}", plugin_path);
}

PluginLoader::~PluginLoader() {
  if (fn_shutdown_ && handle_) {
    shutdown();
  }
  unload();
}

PluginLoader::PluginLoader(PluginLoader &&other) noexcept
    : handle_(other.handle_), plugin_path_(std::move(other.plugin_path_)),
      error_message_(std::move(other.error_message_)),
      fn_get_metadata_(other.fn_get_metadata_),
      fn_initialize_(other.fn_initialize_),
      fn_execute_command_(other.fn_execute_command_),
      fn_shutdown_(other.fn_shutdown_) {
  other.handle_ = nullptr;
  other.fn_get_metadata_ = nullptr;
  other.fn_initialize_ = nullptr;
  other.fn_execute_command_ = nullptr;
  other.fn_shutdown_ = nullptr;
}

PluginLoader &PluginLoader::operator=(PluginLoader &&other) noexcept {
  if (this != &other) {
    unload();

    handle_ = other.handle_;
    plugin_path_ = std::move(other.plugin_path_);
    error_message_ = std::move(other.error_message_);
    fn_get_metadata_ = other.fn_get_metadata_;
    fn_initialize_ = other.fn_initialize_;
    fn_execute_command_ = other.fn_execute_command_;
    fn_shutdown_ = other.fn_shutdown_;

    other.handle_ = nullptr;
    other.fn_get_metadata_ = nullptr;
    other.fn_initialize_ = nullptr;
    other.fn_execute_command_ = nullptr;
    other.fn_shutdown_ = nullptr;
  }
  return *this;
}

void PluginLoader::load_symbols() {
  fn_get_metadata_ = reinterpret_cast<decltype(fn_get_metadata_)>(
      GET_SYMBOL(handle_, "plugin_get_metadata"));

  fn_initialize_ = reinterpret_cast<decltype(fn_initialize_)>(
      GET_SYMBOL(handle_, "plugin_initialize"));

  fn_execute_command_ = reinterpret_cast<decltype(fn_execute_command_)>(
      GET_SYMBOL(handle_, "plugin_execute_command"));

  fn_shutdown_ = reinterpret_cast<decltype(fn_shutdown_)>(
      GET_SYMBOL(handle_, "plugin_shutdown"));
}

void PluginLoader::unload() {
  if (handle_) {
    CLOSE_LIBRARY(handle_);
    handle_ = nullptr;
  }
}

PluginMetadata PluginLoader::get_metadata() const {
  if (!fn_get_metadata_) {
    throw std::runtime_error("Plugin not loaded");
  }
  return fn_get_metadata_();
}

int32_t PluginLoader::initialize(const PluginConfig &config) {
  if (!fn_initialize_) {
    return -1;
  }

  LOG_INFO("PLUGIN", "INIT", "Initializing plugin for instrument: {}",
           config.instrument_name);

  int32_t result = fn_initialize_(&config);

  if (result != 0) {
    LOG_ERROR("PLUGIN", "INIT", "Plugin initialization failed with code: {}",
              result);
  }

  return result;
}

int32_t PluginLoader::execute_command(const PluginCommand &command,
                                      PluginResponse &response) {
  if (!fn_execute_command_) {
    return -1;
  }

  return fn_execute_command_(&command, &response);
}

void PluginLoader::shutdown() {
  if (fn_shutdown_) {
    LOG_INFO("PLUGIN", "SHUTDOWN", "Shutting down plugin:  {}", plugin_path_);
    fn_shutdown_();
  }
}

} // namespace plugin
} // namespace instserver
