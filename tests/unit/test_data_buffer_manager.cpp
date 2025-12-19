#include "instrument-server/ipc/DataBufferManager.hpp"
#include <algorithm>
#include <fstream>
#include <gtest/gtest.h>
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

TEST_F(DataBufferManagerTest, CreateFloat32Buffer) {
  std::vector<float> test_data = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};

  std::string buffer_id =
      manager_->create_buffer("TestInstrument", "MEASURE", DataType::FLOAT32,
                              test_data.size(), test_data.data());

  EXPECT_FALSE(buffer_id.empty());

  auto buffer = manager_->get_buffer(buffer_id);
  ASSERT_NE(buffer, nullptr);

  EXPECT_EQ(buffer->element_count(), test_data.size());
  EXPECT_EQ(buffer->data_type(), DataType::FLOAT32);

  float *data = buffer->as_float32();
  ASSERT_NE(data, nullptr);

  for (size_t i = 0; i < test_data.size(); i++) {
    EXPECT_FLOAT_EQ(data[i], test_data[i]);
  }
}

TEST_F(DataBufferManagerTest, CreateFloat64Buffer) {
  std::vector<double> test_data = {1.5, 2.5, 3.5, 4.5, 5.5};

  std::string buffer_id =
      manager_->create_buffer("TestInstrument", "ACQUIRE", DataType::FLOAT64,
                              test_data.size(), test_data.data());

  EXPECT_FALSE(buffer_id.empty());

  auto buffer = manager_->get_buffer(buffer_id);
  ASSERT_NE(buffer, nullptr);

  EXPECT_EQ(buffer->element_count(), test_data.size());
  EXPECT_EQ(buffer->data_type(), DataType::FLOAT64);

  double *data = buffer->as_float64();
  ASSERT_NE(data, nullptr);

  for (size_t i = 0; i < test_data.size(); i++) {
    EXPECT_DOUBLE_EQ(data[i], test_data[i]);
  }
}

TEST_F(DataBufferManagerTest, CreateLargeBuffer) {
  const size_t element_count = 10000;
  std::vector<float> test_data(element_count);

  // Fill with test pattern
  for (size_t i = 0; i < element_count; i++) {
    test_data[i] = static_cast<float>(i) * 0.1f;
  }

  std::string buffer_id =
      manager_->create_buffer("Oscilloscope", "WAVEFORM", DataType::FLOAT32,
                              test_data.size(), test_data.data());

  EXPECT_FALSE(buffer_id.empty());

  auto buffer = manager_->get_buffer(buffer_id);
  ASSERT_NE(buffer, nullptr);

  EXPECT_EQ(buffer->element_count(), element_count);

  float *data = buffer->as_float32();
  ASSERT_NE(data, nullptr);

  // Spot check values
  EXPECT_FLOAT_EQ(data[0], 0.0f);
  EXPECT_FLOAT_EQ(data[100], 10.0f);
  EXPECT_FLOAT_EQ(data[element_count - 1],
                  static_cast<float>(element_count - 1) * 0.1f);
}

TEST_F(DataBufferManagerTest, GetMetadata) {
  std::vector<int32_t> test_data = {10, 20, 30};

  std::string buffer_id = manager_->create_buffer(
      "DMM", "READ", DataType::INT32, test_data.size(), test_data.data());

  auto metadata = manager_->get_metadata(buffer_id);
  ASSERT_TRUE(metadata.has_value());

  EXPECT_EQ(metadata->buffer_id, buffer_id);
  EXPECT_EQ(metadata->instrument_name, "DMM");
  EXPECT_EQ(metadata->command_id, "READ");
  EXPECT_EQ(metadata->data_type, DataType::INT32);
  EXPECT_EQ(metadata->element_count, test_data.size());
  EXPECT_EQ(metadata->byte_size, test_data.size() * sizeof(int32_t));
  EXPECT_GT(metadata->timestamp_ms, 0);
}

TEST_F(DataBufferManagerTest, Referencecounting) {
  std::vector<float> test_data = {1.0f, 2.0f};

  std::string buffer_id = manager_->create_buffer(
      "Test", "CMD", DataType::FLOAT32, test_data.size(), test_data.data());

  // Get buffer multiple times
  auto buffer1 = manager_->get_buffer(buffer_id);
  auto buffer2 = manager_->get_buffer(buffer_id);
  auto buffer3 = manager_->get_buffer(buffer_id);

  EXPECT_NE(buffer1, nullptr);
  EXPECT_NE(buffer2, nullptr);
  EXPECT_NE(buffer3, nullptr);

  // Release once - buffer should still exist
  manager_->release_buffer(buffer_id);
  auto buffer4 = manager_->get_buffer(buffer_id);
  EXPECT_NE(buffer4, nullptr);

  // Release until count reaches zero
  manager_->release_buffer(buffer_id);
  manager_->release_buffer(buffer_id);
  manager_->release_buffer(buffer_id);
  manager_->release_buffer(buffer_id);

  // Buffer should be gone
  auto buffer5 = manager_->get_buffer(buffer_id);
  EXPECT_EQ(buffer5, nullptr);
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
  std::vector<float> test_data = {1.5f, 2.5f, 3.5f, 4.5f};

  std::string buffer_id = manager_->create_buffer(
      "Test", "CMD", DataType::FLOAT32, test_data.size(), test_data.data());

  auto buffer = manager_->get_buffer(buffer_id);
  ASSERT_NE(buffer, nullptr);

  std::string csv_path = "/tmp/test_buffer.csv";
  EXPECT_TRUE(buffer->export_to_csv(csv_path));

  // Read back and verify
  std::ifstream file(csv_path);
  ASSERT_TRUE(file.is_open());

  std::string line;
  size_t i = 0;
  while (std::getline(file, line) && i < test_data.size()) {
    float value = std::stof(line);
    EXPECT_FLOAT_EQ(value, test_data[i]);
    i++;
  }

  EXPECT_EQ(i, test_data.size());
  file.close();
}

TEST_F(DataBufferManagerTest, ExportToBinary) {
  std::vector<double> test_data = {1.1, 2.2, 3.3, 4.4, 5.5};

  std::string buffer_id = manager_->create_buffer(
      "Test", "CMD", DataType::FLOAT64, test_data.size(), test_data.data());

  auto buffer = manager_->get_buffer(buffer_id);
  ASSERT_NE(buffer, nullptr);

  std::string bin_path = "/tmp/test_buffer.bin";
  EXPECT_TRUE(buffer->export_to_file(bin_path));

  // Read back and verify
  std::ifstream file(bin_path, std::ios::binary);
  ASSERT_TRUE(file.is_open());

  std::vector<double> read_data(test_data.size());
  file.read(reinterpret_cast<char *>(read_data.data()),
            test_data.size() * sizeof(double));

  for (size_t i = 0; i < test_data.size(); i++) {
    EXPECT_DOUBLE_EQ(read_data[i], test_data[i]);
  }

  file.close();
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
