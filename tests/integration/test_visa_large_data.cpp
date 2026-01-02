#include "instrument-server/ipc/DataBufferManager.hpp"
#include "instrument-server/plugin/PluginInterface.h"
#include "instrument-server/plugin/PluginLoader.hpp"
#include <cmath>
#include <filesystem>
#include <gtest/gtest.h>

using namespace instserver;

class VISALargeDataTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Clear any existing buffers
    auto &manager = ipc::DataBufferManager::instance();
    manager.clear_all();

    // This test requires a mock VISA plugin
    plugin_path_ = "./build/tests/mock_visa_large_data_plugin.so";

    if (!std::filesystem::exists(plugin_path_)) {
      GTEST_SKIP() << "Mock VISA plugin not found at: " << plugin_path_;
    }
  }

  void TearDown() override {
    auto &manager = ipc::DataBufferManager::instance();
    manager.clear_all();
  }

  std::string plugin_path_;
};

TEST_F(VISALargeDataTest, SmallDataInResponse) {
  plugin::PluginLoader loader(plugin_path_);
  ASSERT_TRUE(loader.is_loaded());

  // Use a more complete configuration
  PluginConfig config;
  memset(&config, 0, sizeof(PluginConfig));
  strncpy(config.instrument_name, "TestScope", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(config.connection_json, "{\"address\":\"mock://test\"}",
          PLUGIN_MAX_PAYLOAD - 1);
  // Add a default empty API definition if none is provided
  strncpy(config.api_definition_json, "{}", PLUGIN_MAX_PAYLOAD - 1);

  ASSERT_EQ(loader.initialize(config), 0);

  // Request small data (should fit in response)
  PluginCommand cmd = {0};
  strncpy(cmd.id, "cmd_001", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(cmd.instrument_name, "TestScope", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(cmd.verb, "GET_SMALL_DATA", PLUGIN_MAX_STRING_LEN - 1);
  cmd.expects_response = true;
  cmd.param_count = 0;

  PluginResponse resp = {0};
  ASSERT_EQ(loader.execute_command(cmd, resp), 0);

  EXPECT_TRUE(resp.success);
  EXPECT_FALSE(resp.has_large_data);
  EXPECT_GT(strlen(resp.text_response), 0);
}

TEST_F(VISALargeDataTest, LargeDataInBuffer) {
  plugin::PluginLoader loader(plugin_path_);
  ASSERT_TRUE(loader.is_loaded());

  PluginConfig config = {0};
  strncpy(config.instrument_name, "TestScope", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(config.connection_json, "{\"address\":\"mock://test\"}",
          PLUGIN_MAX_PAYLOAD - 1);

  ASSERT_EQ(loader.initialize(config), 0);

  // Request large data (should use buffer)
  PluginCommand cmd = {0};
  strncpy(cmd.id, "cmd_002", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(cmd.instrument_name, "TestScope", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(cmd.verb, "GET_LARGE_DATA", PLUGIN_MAX_STRING_LEN - 1);
  cmd.expects_response = true;
  cmd.param_count = 0;

  PluginResponse resp = {0};
  ASSERT_EQ(loader.execute_command(cmd, resp), 0);

  EXPECT_TRUE(resp.success);
  EXPECT_TRUE(resp.has_large_data);
  EXPECT_GT(strlen(resp.data_buffer_id), 0);
  EXPECT_GT(resp.data_element_count, 1000); // Large data
  EXPECT_EQ(resp.data_type, 0);             // FLOAT32

  // Verify buffer exists
  auto &manager = ipc::DataBufferManager::instance();
  auto buffer = manager.get_buffer(resp.data_buffer_id);
  ASSERT_NE(buffer, nullptr);

  EXPECT_EQ(buffer->element_count(), resp.data_element_count);
  EXPECT_EQ(buffer->data_type(), ipc::DataType::FLOAT32);

  // Verify data content
  float *data = buffer->as_float32();
  ASSERT_NE(data, nullptr);

  // Check some values (mock plugin should generate sin wave)
  for (size_t i = 0; i < 100; i++) {
    float expected = std::sin(2.0f * M_PI * i / 100.0f);
    EXPECT_NEAR(data[i], expected, 0.01f);
  }
}

TEST_F(VISALargeDataTest, BufferMetadata) {
  plugin::PluginLoader loader(plugin_path_);
  ASSERT_TRUE(loader.is_loaded());

  PluginConfig config = {0};
  strncpy(config.instrument_name, "Oscilloscope", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(config.connection_json, "{\"address\": \"mock://test\"}",
          PLUGIN_MAX_PAYLOAD - 1);

  ASSERT_EQ(loader.initialize(config), 0);

  PluginCommand cmd = {0};
  strncpy(cmd.id, "waveform_capture_001", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(cmd.instrument_name, "Oscilloscope", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(cmd.verb, "GET_LARGE_DATA", PLUGIN_MAX_STRING_LEN - 1);
  cmd.expects_response = true;

  PluginResponse resp = {0};
  ASSERT_EQ(loader.execute_command(cmd, resp), 0);
  ASSERT_TRUE(resp.has_large_data);

  // Check metadata
  auto &manager = ipc::DataBufferManager::instance();
  auto metadata = manager.get_metadata(resp.data_buffer_id);
  ASSERT_TRUE(metadata.has_value());

  EXPECT_EQ(metadata->instrument_name, "Oscilloscope");
  EXPECT_EQ(metadata->command_id, "waveform_capture_001");
  EXPECT_EQ(metadata->data_type, ipc::DataType::FLOAT32);
  EXPECT_GT(metadata->timestamp_ms, 0);
}

TEST_F(VISALargeDataTest, ExportLargeData) {
  plugin::PluginLoader loader(plugin_path_);
  ASSERT_TRUE(loader.is_loaded());

  PluginConfig config = {0};
  strncpy(config.instrument_name, "TestScope", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(config.connection_json, "{\"address\":\"mock://test\"}",
          PLUGIN_MAX_PAYLOAD - 1);

  ASSERT_EQ(loader.initialize(config), 0);

  PluginCommand cmd = {0};
  strncpy(cmd.id, "export_test", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(cmd.instrument_name, "TestScope", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(cmd.verb, "GET_LARGE_DATA", PLUGIN_MAX_STRING_LEN - 1);
  cmd.expects_response = true;

  PluginResponse resp = {0};
  ASSERT_EQ(loader.execute_command(cmd, resp), 0);
  ASSERT_TRUE(resp.has_large_data);

  auto &manager = ipc::DataBufferManager::instance();
  auto buffer = manager.get_buffer(resp.data_buffer_id);
  ASSERT_NE(buffer, nullptr);

  // Export to CSV
  std::string csv_path = "/tmp/visa_large_data_test.csv";
  EXPECT_TRUE(buffer->export_to_csv(csv_path));
  EXPECT_TRUE(std::filesystem::exists(csv_path));

  // Export to binary
  std::string bin_path = "/tmp/visa_large_data_test.bin";
  EXPECT_TRUE(buffer->export_to_file(bin_path));
  EXPECT_TRUE(std::filesystem::exists(bin_path));

  // Cleanup
  std::filesystem::remove(csv_path);
  std::filesystem::remove(bin_path);
}

TEST_F(VISALargeDataTest, MultipleBuffers) {
  plugin::PluginLoader loader(plugin_path_);
  ASSERT_TRUE(loader.is_loaded());

  PluginConfig config = {0};
  strncpy(config.instrument_name, "TestScope", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(config.connection_json, "{\"address\":\"mock://test\"}",
          PLUGIN_MAX_PAYLOAD - 1);

  ASSERT_EQ(loader.initialize(config), 0);

  auto &manager = ipc::DataBufferManager::instance();

  std::vector<std::string> buffer_ids;

  // Create multiple buffers
  for (int i = 0; i < 5; i++) {
    PluginCommand cmd = {0};
    snprintf(cmd.id, PLUGIN_MAX_STRING_LEN, "cmd_%03d", i);
    strncpy(cmd.instrument_name, "TestScope", PLUGIN_MAX_STRING_LEN - 1);
    strncpy(cmd.verb, "GET_LARGE_DATA", PLUGIN_MAX_STRING_LEN - 1);
    cmd.expects_response = true;

    PluginResponse resp = {0};
    ASSERT_EQ(loader.execute_command(cmd, resp), 0);
    ASSERT_TRUE(resp.has_large_data);

    buffer_ids.push_back(resp.data_buffer_id);
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
