#include "PlatformPaths.hpp"
#include "instrument-script-server/ipc/DataBufferManager.hpp"
#include "instrument-script-server/plugin/PluginLoader.hpp"
#include <algorithm>
#include <fmt/format.h>
#include <format>
#include <instrument-plugin.h>
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

    if (!std::filesystem::exists(plugin_path_)) {
      GTEST_SKIP() << "Mock VISA plugin not found at:  " << plugin_path_;
    }
  }

  void TearDown() override {
    auto &manager = ipc::DataBufferManager::instance();
    manager.clear_all();
  }

  std::filesystem::path plugin_path_;
};

TEST_F(VISALargeDataTest, SmallDataInResponse) {
  plugin::PluginLoader loader(plugin_path_.string());
  ASSERT_TRUE(loader.is_loaded());

  // Use a more complete configuration
  PluginConfig config{};
  safe_c_str_copy(config.instrument_name, "TestScope");
  safe_c_str_copy(config.connection_json, R"({"address":"mock://test"})");
  safe_c_str_copy(config.api_definition_json, "{}");

  ASSERT_EQ(loader.initialize(config), 0);

  // Request small data (should fit in response)
  PluginCommand cmd{};
  safe_c_str_copy(cmd.id, "cmd_001");
  safe_c_str_copy(cmd.instrument_name, "TestScope");
  safe_c_str_copy(cmd.verb, "GET_SMALL_DATA");
  cmd.expects_response = true;
  cmd.param_count = 0;

  PluginResponse resp{};
  ASSERT_EQ(loader.execute_command(cmd, resp), 0);

  EXPECT_TRUE(resp.success);
  EXPECT_FALSE(resp.has_large_data);
  EXPECT_GT(std::string_view(resp.text_response).size(), 0);
}

TEST_F(VISALargeDataTest, LargeDataInBuffer) {
  plugin::PluginLoader loader(plugin_path_.string());
  ASSERT_TRUE(loader.is_loaded());

  PluginConfig config{};
  safe_c_str_copy(config.instrument_name, "TestScope");
  safe_c_str_copy(config.connection_json, R"({"address":"mock://test"})");

  ASSERT_EQ(loader.initialize(config), 0);

  // Request large data (should use buffer)
  PluginCommand cmd{};
  safe_c_str_copy(cmd.id, "cmd_002");
  safe_c_str_copy(cmd.instrument_name, "TestScope");
  safe_c_str_copy(cmd.verb, "GET_LARGE_DATA");
  cmd.expects_response = true;
  cmd.param_count = 0;

  PluginResponse resp{};
  ASSERT_EQ(loader.execute_command(cmd, resp), 0);

  EXPECT_TRUE(resp.success);
  EXPECT_TRUE(resp.has_large_data);
  EXPECT_GT(strlen(resp.data_buffer_id), 0);
  EXPECT_GT(resp.data_element_count, 1000); // Large data
  EXPECT_EQ(resp.data_type, INST_DATA_FLOAT32);

  // Verify buffer exists
  auto &manager = ipc::DataBufferManager::instance();
  auto meta = manager.get_metadata(resp.data_buffer_id);
  ASSERT_TRUE(meta.has_value());

  EXPECT_EQ(meta->element_count, resp.data_element_count);
  EXPECT_EQ(meta->data_type, INST_DATA_FLOAT32);
}

TEST_F(VISALargeDataTest, BufferMetadata) {
  plugin::PluginLoader loader(plugin_path_.string());
  ASSERT_TRUE(loader.is_loaded());

  PluginConfig config{};
  safe_c_str_copy(config.instrument_name, "Oscilloscope");
  safe_c_str_copy(config.connection_json, R"({"address":"mock://test"})");

  ASSERT_EQ(loader.initialize(config), 0);

  PluginCommand cmd{};
  safe_c_str_copy(cmd.id, "waveform_capture_001");
  safe_c_str_copy(cmd.instrument_name, "Oscilloscope");
  safe_c_str_copy(cmd.verb, "GET_LARGE_DATA");
  cmd.expects_response = true;

  PluginResponse resp{};
  ASSERT_EQ(loader.execute_command(cmd, resp), 0);
  ASSERT_TRUE(resp.has_large_data);

  // Check metadata
  auto &manager = ipc::DataBufferManager::instance();
  auto metadata = manager.get_metadata(resp.data_buffer_id);
  ASSERT_TRUE(metadata.has_value());

  EXPECT_EQ(metadata->instrument_name, "Oscilloscope");
  EXPECT_EQ(metadata->command_id, "waveform_capture_001");
  EXPECT_EQ(metadata->data_type, INST_DATA_FLOAT32);
  EXPECT_GT(metadata->timestamp_ms, 0);
}

TEST_F(VISALargeDataTest, MultipleBuffers) {
  plugin::PluginLoader loader(plugin_path_.string());
  ASSERT_TRUE(loader.is_loaded());

  PluginConfig config{};
  safe_c_str_copy(config.instrument_name, "TestScope");
  safe_c_str_copy(config.connection_json, R"({"address":"mock://test"})");

  ASSERT_EQ(loader.initialize(config), 0);

  auto &manager = ipc::DataBufferManager::instance();

  std::vector<std::string> buffer_ids;

  // Create multiple buffers
  const size_t total_buffers = 5;
  for (int i = 0; i < total_buffers; i++) {
    PluginCommand cmd{};
    auto tmp = fmt::format("cmd_{:03d}", i);
    safe_c_str_copy(cmd.id, tmp);
    safe_c_str_copy(cmd.instrument_name, "TestScope");
    safe_c_str_copy(cmd.verb, "GET_LARGE_DATA");
    cmd.expects_response = true;

    PluginResponse resp{};
    ASSERT_EQ(loader.execute_command(cmd, resp), 0);
    ASSERT_TRUE(resp.has_large_data);

    buffer_ids.emplace_back(resp.data_buffer_id);
  }

  // Verify all buffers exist
  auto buffers = manager.list_buffers();
  EXPECT_GE(buffers.size(), 5);

  // Verify memory tracking
  EXPECT_GT(manager.total_memory_usage(), 0);

  // Release all buffers
  for (const auto &id : buffer_ids) {
    manager.release_buffer(id);
  }
}
