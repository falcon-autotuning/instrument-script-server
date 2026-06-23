#include "instrument-script-server/ipc/DataBufferManager.hpp"
#include <algorithm>
#include <gtest/gtest.h>
#include <instrument-data.h>
#include <google/protobuf/util/time_util.h>

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
  manager_->save_buffer(buffer_id);
  auto metadata = manager_->get_metadata(buffer_id);
  ASSERT_TRUE(metadata.has_value());

  EXPECT_EQ(metadata->instrument_name(), "DMM");
  EXPECT_EQ(metadata->command_id(), "READ");
  EXPECT_EQ(metadata->data_type(), INST_DATA_INT32);
  EXPECT_EQ(metadata->element_count(), test_data.size());
  EXPECT_EQ(metadata->byte_size(), test_data.size() * sizeof(int32_t));
  EXPECT_GT(google::protobuf::util::TimeUtil::TimestampToMilliseconds(metadata->captured_at()), 0);
  manager_->release_buffer(buffer_id);
}

TEST_F(DataBufferManagerTest, Referencecounting) {
  std::vector<float> test_data = {1.0f, 2.0f};
  const char *buffer_id = data_manager_create_buffer(
      "Test", "CMD", INST_DATA_FLOAT32, test_data.size(), test_data.data());

  // Save buffer multiple times. But 1 process
  manager_->save_buffer(buffer_id);
  manager_->save_buffer(buffer_id);
  manager_->save_buffer(buffer_id);

  // Metadata should be retrievable
  EXPECT_TRUE(manager_->get_metadata(buffer_id) != std::nullopt);

  // Release the global buffer (decrements global owners, but process-local
  // stays alive while wrappers are alive)
  manager_->release_buffer(buffer_id);

  // Now Metadata should not be retrievable
  EXPECT_TRUE(manager_->get_metadata(buffer_id) == std::nullopt);
}

TEST_F(DataBufferManagerTest, ListBuffers) {
  EXPECT_TRUE(manager_->list_buffers().empty());

  std::vector<float> data1 = {1.0F};
  std::vector<float> data2 = {2.0F};
  std::vector<float> data3 = {3.0F};

  const char *id1 = data_manager_create_buffer("I1", "C1", INST_DATA_FLOAT32,
                                               data1.size(), data1.data());
  const char *id2 = data_manager_create_buffer("I2", "C2", INST_DATA_FLOAT32,
                                               data2.size(), data2.data());
  const char *id3 = data_manager_create_buffer("I3", "C3", INST_DATA_FLOAT32,
                                               data3.size(), data3.data());
  manager_->save_buffer(id1);
  manager_->save_buffer(id2);
  manager_->save_buffer(id3);
  auto buffers = manager_->list_buffers();

  EXPECT_EQ(buffers.size(), 3);
  EXPECT_NE(std::find(buffers.begin(), buffers.end(), id1), buffers.end());
  EXPECT_NE(std::find(buffers.begin(), buffers.end(), id2), buffers.end());
  EXPECT_NE(std::find(buffers.begin(), buffers.end(), id3), buffers.end());
  manager_->release_buffer(id1);
  manager_->release_buffer(id2);
  manager_->release_buffer(id3);
}

TEST_F(DataBufferManagerTest, TotalMemoryUsage) {
  const size_t data1_size = 100;
  const size_t data2_size = 200;

  EXPECT_EQ(manager_->total_memory_usage(), 0);

  std::vector<float> data1(data1_size);
  std::vector<double> data2(data2_size);
  const char *id1 = data_manager_create_buffer("I1", "C1", INST_DATA_FLOAT32,
                                               data1.size(), data1.data());
  const char *id2 = data_manager_create_buffer("I2", "C2", INST_DATA_FLOAT64,
                                               data2.size(), data2.data());
  manager_->save_buffer(id1);
  manager_->save_buffer(id2);

  EXPECT_EQ(manager_->list_buffers().size(), 2);
  size_t expected =
      (data1_size * sizeof(float)) + (data2_size * sizeof(double));
  EXPECT_EQ(manager_->total_memory_usage(), expected);

  manager_->clear_all();

  EXPECT_EQ(manager_->list_buffers().size(), 0);
  EXPECT_EQ(manager_->total_memory_usage(), 0);
}

TEST_F(DataBufferManagerTest, InvalidBufferId) {
  auto metadata = manager_->get_metadata("nonexistent_buffer_id");
  EXPECT_FALSE(metadata.has_value());
}
