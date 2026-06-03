#include "instrument-script-server/ipc/DataBufferManager.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <instrument-data.h>
#include <instrument-plugin.h>
#include <vector>

using namespace instserver::ipc;

class DataBufferManagerTest : public ::testing::Test {
protected:
  void SetUp() override {
    manager_ = &DataBufferManager::instance();
    manager_->clear_all();
  }

  void TearDown() override { manager_->clear_all(); }

  DataBufferManager *manager_;
};

TEST_F(DataBufferManagerTest, GetMetadata) {
  std::vector<int32_t> test_data = {10, 20, 30};
  const char *buffer_id = data_manager_create_buffer(
      "DMM", "READ", INST_DATA_INT32, test_data.size(), test_data.data());

  auto metadata = manager_->get_metadata(buffer_id);
  ASSERT_TRUE(metadata.has_value());

  EXPECT_EQ(metadata->buffer_id, buffer_id);
  EXPECT_EQ(metadata->instrument_name, "DMM");
  EXPECT_EQ(metadata->command_id, "READ");
  EXPECT_EQ(metadata->data_type, INST_DATA_INT32);
  EXPECT_EQ(metadata->element_count, test_data.size());
  EXPECT_EQ(metadata->byte_size, test_data.size() * sizeof(int32_t));
  EXPECT_GT(metadata->timestamp_ms, 0);
}

TEST_F(DataBufferManagerTest, Referencecounting) {
  std::vector<float> test_data = {1.0f, 2.0f};
  const char *buffer_id = data_manager_create_buffer(
      "Test", "CMD", INST_DATA_FLOAT32, test_data.size(), test_data.data());

  // Save buffer multiple times. But 1 process
  manager_->save_buffer(buffer_id);
  manager_->save_buffer(buffer_id);
  manager_->save_buffer(buffer_id);

  // Release the global buffer (decrements global owners, but process-local
  // stays alive while wrappers are alive)
  manager_->release_buffer(buffer_id);

  // We can still access the buffer via active wrappers or get_buffer (since
  // process-local is alive)
  auto buffer4 = manager_->get_buffer(buffer_id);
  EXPECT_EQ(buffer4, nullptr);
}

TEST_F(DataBufferManagerTest, ListBuffers) {
  EXPECT_TRUE(manager_->list_buffers().empty());

  std::vector<float> data1 = {1.0f};
  std::vector<float> data2 = {2.0f};
  std::vector<float> data3 = {3.0f};

  std::string id1 =
      manager_->create_buffer("I1", "C1", DataType::FLOAT32, 1, data1.data());
  std::string id2 =
      manager_->create_buffer("I2", "C2", DataType::FLOAT32, 1, data2.data());
  std::string id3 =
      manager_->create_buffer("I3", "C3", DataType::FLOAT32, 1, data3.data());

  auto buffers = manager_->list_buffers();
  EXPECT_EQ(buffers.size(), 3);

  EXPECT_NE(std::find(buffers.begin(), buffers.end(), id1), buffers.end());
  EXPECT_NE(std::find(buffers.begin(), buffers.end(), id2), buffers.end());
  EXPECT_NE(std::find(buffers.begin(), buffers.end(), id3), buffers.end());
}

TEST_F(DataBufferManagerTest, TotalMemoryUsage) {
  EXPECT_EQ(manager_->total_memory_usage(), 0);

  std::vector<float> data1(100);
  std::vector<double> data2(200);

  manager_->create_buffer("I1", "C1", DataType::FLOAT32, data1.size(),
                          data1.data());
  manager_->create_buffer("I2", "C2", DataType::FLOAT64, data2.size(),
                          data2.data());

  size_t expected = (100 * sizeof(float)) + (200 * sizeof(double));
  EXPECT_EQ(manager_->total_memory_usage(), expected);
}

TEST_F(DataBufferManagerTest, ExportToCSV) {
  manager_->clear_all();

  // Create test data
  std::vector<float> data = {1.1f, 2.2f, 3.3f, 4.4f};
  std::string buffer_id =
      manager_->create_buffer("Test", "CMD", instserver::ipc::DataType::FLOAT32,
                              data.size(), data.data());

  auto buffer = manager_->get_buffer(buffer_id);
  ASSERT_NE(buffer, nullptr);

  // Use cross-platform temp directory
  auto temp_dir = std::filesystem::temp_directory_path();
  std::string csv_path = (temp_dir / "test_export.csv").string();

  EXPECT_TRUE(buffer->export_to_csv(csv_path));

  // Verify file was created
  std::ifstream file(csv_path);
  EXPECT_TRUE(file.is_open());

  if (file.is_open()) {
    std::string line;
    std::getline(file, line);
    EXPECT_FALSE(line.empty());
    file.close();
  }

  // Cleanup
  std::filesystem::remove(csv_path);
}

TEST_F(DataBufferManagerTest, ExportToBinary) {
  manager_->clear_all();

  // Create test data
  std::vector<double> data = {10.5, 20.5, 30.5, 40.5, 50.5};
  std::string buffer_id =
      manager_->create_buffer("Test", "CMD", instserver::ipc::DataType::FLOAT64,
                              data.size(), data.data());

  auto buffer = manager_->get_buffer(buffer_id);
  ASSERT_NE(buffer, nullptr);

  // Use cross-platform temp directory
  auto temp_dir = std::filesystem::temp_directory_path();
  std::string bin_path = (temp_dir / "test_export.bin").string();

  EXPECT_TRUE(buffer->export_to_file(bin_path));

  // Verify file was created and has correct size
  std::ifstream file(bin_path, std::ios::binary);
  EXPECT_TRUE(file.is_open());

  if (file.is_open()) {
    file.seekg(0, std::ios::end);
    size_t file_size = file.tellg();
    EXPECT_EQ(file_size, data.size() * sizeof(double));
    file.close();
  }

  // Cleanup
  std::filesystem::remove(bin_path);
}

TEST_F(DataBufferManagerTest, TypeSafety) {
  std::vector<float> test_data = {1.0f, 2.0f, 3.0f};

  std::string buffer_id = manager_->create_buffer(
      "Test", "CMD", DataType::FLOAT32, test_data.size(), test_data.data());

  auto buffer = manager_->get_buffer(buffer_id);
  ASSERT_NE(buffer, nullptr);

  // Correct type should work
  EXPECT_NE(buffer->as_float32(), nullptr);

  // Wrong types should return nullptr
  EXPECT_EQ(buffer->as_float64(), nullptr);
  EXPECT_EQ(buffer->as_int32(), nullptr);
  EXPECT_EQ(buffer->as_int64(), nullptr);
}

TEST_F(DataBufferManagerTest, InvalidBufferId) {
  auto buffer = manager_->get_buffer("nonexistent_buffer_id");
  EXPECT_EQ(buffer, nullptr);

  auto metadata = manager_->get_metadata("nonexistent_buffer_id");
  EXPECT_FALSE(metadata.has_value());
}

TEST_F(DataBufferManagerTest, ClearAll) {
  std::vector<float> data(10);
  manager_->create_buffer("I1", "C1", DataType::FLOAT32, 10, data.data());
  manager_->create_buffer("I2", "C2", DataType::FLOAT32, 10, data.data());

  EXPECT_EQ(manager_->list_buffers().size(), 2);

  manager_->clear_all();

  EXPECT_EQ(manager_->list_buffers().size(), 0);
  EXPECT_EQ(manager_->total_memory_usage(), 0);
}
