#include "instrument-script-server/plugin/PluginLoader.hpp"
#include "instrument-script-server/ErrorCodes.hpp"
#include <instrument-log/inst_logging.h>
#include <stdexcept>

namespace instserver::plugin {

#ifdef _WIN32
#define LOAD_LIBRARY(path) LoadLibraryA(path)
#define GET_SYMBOL(handle, name) GetProcAddress(handle, name)
#define CLOSE_LIBRARY(handle) FreeLibrary(handle)
std::string get_last_error() {
  DWORD err = GetLastError();
  LPSTR buf = nullptr;

  FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                     FORMAT_MESSAGE_IGNORE_INSERTS,
                 NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                 (LPSTR)&buf, 0, NULL);

  std::string msg = buf ? buf : "Unknown error";
  if (buf)
    LocalFree(buf);
  return msg;
}

#define LIBRARY_ERROR() get_last_error()
#else
#define LOAD_LIBRARY(path) dlopen(path, RTLD_LAZY)
#define GET_SYMBOL(handle, name) dlsym(handle, name)
#define CLOSE_LIBRARY(handle) dlclose(handle)
#define LIBRARY_ERROR() dlerror()
#endif

PluginLoader::PluginLoader(const std::string &plugin_path)
    : handle_(LOAD_LIBRARY(plugin_path.c_str())), plugin_path_(plugin_path) {

  LOG_INFO("PLUGIN", "LOAD", "Loading plugin: %s", plugin_path.c_str());

  if (handle_ == nullptr) {
    error_message_ = std::string("Failed to load library: ") + LIBRARY_ERROR();
    LOG_ERROR("PLUGIN", "LOAD", "%s", error_message_.c_str());
    throw std::runtime_error(error_message_);
  }

  load_symbols();

  if ((fn_get_metadata_ == nullptr) || (fn_initialize_ == nullptr) ||
      (fn_execute_command_ == nullptr) || (fn_shutdown_ == nullptr)) {
    error_message_ = "Failed to load required plugin symbols";
    LOG_ERROR("PLUGIN", "LOAD", "%s", error_message_.c_str());
    if (fn_get_metadata_ == nullptr) {
      LOG_ERROR("PLUGIN", "LOAD", "Missing: plugin_get_metadata");
    }
    if (fn_initialize_ == nullptr) {
      LOG_ERROR("PLUGIN", "LOAD", "Missing: plugin_initialize");
    }
    if (fn_execute_command_ == nullptr) {
      LOG_ERROR("PLUGIN", "LOAD", "Missing: plugin_execute_command");
    }
    if (fn_shutdown_ == nullptr) {
      LOG_ERROR("PLUGIN", "LOAD", "Missing: plugin_shutdown");
    }
    unload();
    throw std::runtime_error(error_message_);
  }

  LOG_INFO("PLUGIN", "LOAD", "Plugin loaded successfully:  %s",
           plugin_path.c_str());
}

PluginLoader::~PluginLoader() {}

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
  if (handle_ != nullptr) {
    CLOSE_LIBRARY(handle_);
    handle_ = nullptr;
  }
}

PluginMetadata PluginLoader::get_metadata() const {
  if (fn_get_metadata_ == nullptr) {
    throw std::runtime_error("Plugin not loaded");
  }
  return fn_get_metadata_();
}

ErrorCode PluginLoader::initialize(const PluginConfig *config) {
  if (fn_initialize_ == nullptr) {
    return ErrorCode::INITIALIZE_NOT_PROVIDED;
  }

  uint8_t result = fn_initialize_(config);

  if (result != 0) {
    LOG_ERROR("PLUGIN", "INIT", "Plugin initialization failed with code: %d",
              result);
  }

  return ErrorCode(result);
}

ErrorCode PluginLoader::execute_command(const PluginCommand *command,
                                        PluginResponse *response) {
  if (fn_execute_command_ == nullptr) {
    return ErrorCode::EXECUTE_COMMAND_NOT_PROVIDED;
  }
  auto out = ErrorCode(fn_execute_command_(command, response));
  return out;
}

void PluginLoader::shutdown() {
  if (!shutdown_called_ && (fn_shutdown_ != nullptr)) {
    LOG_INFO("PLUGIN", "SHUTDOWN", "Shutting down plugin: %s",
             plugin_path_.c_str());

    fn_shutdown_();
    shutdown_called_ = true;
  }
}

} // namespace instserver::plugin
