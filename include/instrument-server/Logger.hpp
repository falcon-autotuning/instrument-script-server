#pragma once
#include <fmt/format.h>
#include <memory>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <string>

namespace instserver {

/// Centralized logging with instruction ID and instrument name context
class InstrumentLogger {
public:
  static InstrumentLogger &instance() {
    static InstrumentLogger logger;
    return logger;
  }

  // Initialize with file and console sinks
  void init(const std::string &log_file = "instrument_server.log",
            spdlog::level::level_enum level = spdlog::level::debug) {
    std::lock_guard<std::mutex> lock(mutex_);

    // If already initialized, just update level
    if (logger_) {
      logger_->set_level(level);
      // ensure flush behavior follows requested level
      logger_->flush_on(level);
      return;
    }

    try {
      auto console_sink =
          std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
      console_sink->set_level(spdlog::level::info);

      auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
          log_file, 1024 * 1024 * 10, 3); // 10MB, 3 files
      file_sink->set_level(spdlog::level::trace);

      std::vector<spdlog::sink_ptr> sinks{console_sink, file_sink};
      logger_ = std::make_shared<spdlog::logger>("instrument", sinks.begin(),
                                                 sinks.end());
      logger_->set_level(level);
      // flush on the requested level so tests can rely on timely flushes
      logger_->flush_on(level);

      // Don't register if already exists
      if (!spdlog::get("instrument")) {
        spdlog::register_logger(logger_);
      }
    } catch (const spdlog::spdlog_ex &ex) {
      // Silently ignore if already initialized
      if (std::string(ex.what()).find("already exists") == std::string::npos) {
        fmt::print(stderr, "Log initialization failed: {}\n", ex.what());
      }
    }
  }

  // Shutdown the logger: drop from spdlog registry and clear internal pointer.
  // This allows subsequent init() calls to recreate sinks (useful for tests).
  void shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    try {
      // drop the named logger from spdlog registry
      spdlog::drop("instrument");
    } catch (...) {
      // ignore drop errors
    }
    logger_.reset();
  }

  template <typename... Args>
  void trace(const std::string &instr_name, const std::string &instr_id,
             const std::string &fmt_str, Args &&...args) {
    log(spdlog::level::trace, instr_name, instr_id, fmt_str,
        std::forward<Args>(args)...);
  }

  template <typename... Args>
  void debug(const std::string &instr_name, const std::string &instr_id,
             const std::string &fmt_str, Args &&...args) {
    log(spdlog::level::debug, instr_name, instr_id, fmt_str,
        std::forward<Args>(args)...);
  }

  template <typename... Args>
  void info(const std::string &instr_name, const std::string &instr_id,
            const std::string &fmt_str, Args &&...args) {
    log(spdlog::level::info, instr_name, instr_id, fmt_str,
        std::forward<Args>(args)...);
  }

  template <typename... Args>
  void warn(const std::string &instr_name, const std::string &instr_id,
            const std::string &fmt_str, Args &&...args) {
    log(spdlog::level::warn, instr_name, instr_id, fmt_str,
        std::forward<Args>(args)...);
  }

  template <typename... Args>
  void error(const std::string &instr_name, const std::string &instr_id,
             const std::string &fmt_str, Args &&...args) {
    log(spdlog::level::err, instr_name, instr_id, fmt_str,
        std::forward<Args>(args)...);
  }

private:
  InstrumentLogger() = default;

  template <typename... Args>
  void log(spdlog::level::level_enum level, const std::string &instr_name,
           const std::string &instr_id, const std::string &fmt_str,
           Args &&...args) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!logger_)
      return;

    // Format:  [instrument_name] [instruction_id] message
    std::string prefix = fmt::format("[{}] [{}] ", instr_name, instr_id);
    std::string full_msg = prefix + fmt::format(fmt::runtime(fmt_str),
                                                std::forward<Args>(args)...);
    logger_->log(level, full_msg);
  }

  std::shared_ptr<spdlog::logger> logger_;
  std::mutex mutex_;
};

// Convenience macros
#define LOG_TRACE(instr, id, ...)                                              \
  instserver::InstrumentLogger::instance().trace(instr, id, __VA_ARGS__)
#define LOG_DEBUG(instr, id, ...)                                              \
  instserver::InstrumentLogger::instance().debug(instr, id, __VA_ARGS__)
#define LOG_INFO(instr, id, ...)                                               \
  instserver::InstrumentLogger::instance().info(instr, id, __VA_ARGS__)
#define LOG_WARN(instr, id, ...)                                               \
  instserver::InstrumentLogger::instance().warn(instr, id, __VA_ARGS__)
#define LOG_ERROR(instr, id, ...)                                              \
  instserver::InstrumentLogger::instance().error(instr, id, __VA_ARGS__)

} // namespace instserver
