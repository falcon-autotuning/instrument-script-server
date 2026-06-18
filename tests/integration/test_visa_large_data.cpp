#include "PlatformPaths.hpp"
#include "instrument-script-server/ErrorCodes.hpp"
#include "instrument-script-server/ipc/DataBufferManager.hpp"
#include "instrument-script-server/plugin/PluginLoader.hpp"
#include <algorithm>
#include <fmt/format.h>
#include <inst_logging.h>
#include <instrument-data.h>
#include <instrument-plugin.h>
#include <plugin-api.h>
#include <plugin-host.h>
#include <string_view>

// CRITICAL: Define this BEFORE including <cmath> to get M_PI on Windows
#define _USE_MATH_DEFINES
#include <cmath>

// Fallback for systems that don't define M_PI even with _USE_MATH_DEFINES
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include <filesystem>
#include <gtest/gtest.h>
// Helper to safely fill a fixed C-array from a C++ string literal
namespace {
template <typename T>
inline void safe_c_str_copy(T &dest, std::string_view src) {
  constexpr size_t N = sizeof(dest);
  const size_t bytes_to_copy = std::min(src.size(), N - 1);
  char *raw_dest = static_cast<char *>(std::addressof(dest[0]));
  std::copy_n(src.begin(), bytes_to_copy, raw_dest);
  const char null_terminator = '\0';
  std::copy_n(&null_terminator, 1,
              std::next(raw_dest, static_cast<std::ptrdiff_t>(bytes_to_copy)));
}
} // namespace
using namespace instserver;
using namespace instserver::test;

class VISALargeDataTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Clear any existing buffers
    auto &manager = ipc::DataBufferManager::instance();
    manager.clear_all();

    plugin_path_ = get_test_plugin_path("mock_visa_large_data_plugin");

    // Initialize logger
    auto log_path =
        std::filesystem::temp_directory_path() / "test_visa_large_data.log";
    inst_log_shutdown();
    inst_log_init(log_path.string().c_str(), INST_LOG_DEBUG, "instrument",
                  1024 * 1024 * 10, // 10 MB
                  3);               // rotation count
    if (!std::filesystem::exists(plugin_path_)) {
      GTEST_SKIP() << "Mock VISA plugin not found at:  " << plugin_path_;
    }
  }

  void TearDown() override {
    auto &manager = ipc::DataBufferManager::instance();
    manager.clear_all();
    inst_log_flush();
    inst_log_shutdown();
  }

  std::filesystem::path plugin_path_;
};

TEST_F(VISALargeDataTest, SmallDataInResponse) {
  plugin::PluginLoader loader(plugin_path_.string());
  ASSERT_TRUE(loader.is_loaded());

  // Use a more complete configuration
  PluginConfig config{};
  safe_c_str_copy(config.instrument_name, "TestScope");
  safe_c_str_copy(config.address, "mock://test");
  safe_c_str_copy(config.custom, "");
  config.baud_rate = 0;

  ASSERT_EQ(loader.initialize(&config), ErrorCode::NONE);

  // Request small data (should fit in response)
  PluginCommand cmd{};
  safe_c_str_copy(cmd.id, "cmd_001");
  safe_c_str_copy(cmd.command, "GET_SMALL_DATA");
  cmd.timeout_ms = 10;
  cmd.params = param_storage_create();

  PluginResponse *resp = plugin_response_create();
  ASSERT_EQ(loader.execute_command(&cmd, resp), ErrorCode::NONE);
  const Variable *v = plugin_response_get(resp, 0);
  ASSERT_STREQ(v->name, "data");
  ASSERT_EQ(v->type, PARAM_TYPE_DOUBLE);
  ASSERT_EQ(v->value.d_val, 42.0);

  param_storage_free(cmd.params);
  plugin_response_free(resp);
}

TEST_F(VISALargeDataTest, LargeDataInBuffer) {
  plugin::PluginLoader loader(plugin_path_.string());
  ASSERT_TRUE(loader.is_loaded());

  PluginConfig config{};
  safe_c_str_copy(config.instrument_name, "TestScope");
  safe_c_str_copy(config.address, "mock://test");
  safe_c_str_copy(config.custom, "");
  config.baud_rate = 0;

  ASSERT_EQ(loader.initialize(&config), ErrorCode::NONE);

  // Request large data (should use buffer)
  PluginCommand cmd{};
  safe_c_str_copy(cmd.id, "cmd_002");
  safe_c_str_copy(cmd.command, "GET_LARGE_DATA");
  cmd.params = param_storage_create();

  PluginResponse *resp = plugin_response_create();
  ASSERT_EQ(loader.execute_command(&cmd, resp), ErrorCode::NONE);

  const Variable *v = plugin_response_get(resp, 0);
  ASSERT_STREQ(v->name, "data");
  ASSERT_EQ(v->type, PARAM_TYPE_BUFFER);

  // Verify buffer exists
  ASSERT_TRUE(data_manager_get_buffer(v->value.str_val) != nullptr);
  data_manager_release_buffer(v->value.str_val); // Clean up
  param_storage_free(cmd.params);
  plugin_response_free(resp);
}
