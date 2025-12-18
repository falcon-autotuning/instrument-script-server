#include "instrument-server/ipc/ProcessManager.hpp"
#include "instrument-server/ipc/SharedQueue.hpp"
#include <chrono>
#include <gtest/gtest.h>

using namespace instserver::ipc;

class SharedQueueTest : public ::testing::Test {
protected:
  void SetUp() override {
    instrument_name_ = "test_queue_" + std::to_string(std::time(nullptr));
  }

  void TearDown() override { SharedQueue::cleanup(instrument_name_); }

  std::string instrument_name_;
};

TEST_F(SharedQueueTest, CreateServerQueue) {
  EXPECT_NO_THROW({
    auto queue = SharedQueue::create_server_queue(instrument_name_);
    EXPECT_TRUE(queue->is_valid());
  });
}

TEST_F(SharedQueueTest, CreateWorkerQueue) {
  auto server_queue = SharedQueue::create_server_queue(instrument_name_);

  EXPECT_NO_THROW({
    auto worker_queue = SharedQueue::create_worker_queue(instrument_name_);
    EXPECT_TRUE(worker_queue->is_valid());
  });
}

TEST_F(SharedQueueTest, SendReceiveCommand) {
  auto server_queue = SharedQueue::create_server_queue(instrument_name_);
  auto worker_queue = SharedQueue::create_worker_queue(instrument_name_);

  IPCMessage msg;
  msg.type = IPCMessage::Type::COMMAND;
  msg.id = 42;
  std::string payload = "{\"test\": \"data\"}";
  msg.payload_size = payload.size();
  std::memcpy(msg.payload, payload.c_str(), payload.size());

  EXPECT_TRUE(server_queue->send(msg, std::chrono::milliseconds(1000)));

  auto received = worker_queue->receive(std::chrono::milliseconds(1000));
  ASSERT_TRUE(received.has_value());
  EXPECT_EQ(received->type, IPCMessage::Type::COMMAND);
  EXPECT_EQ(received->id, 42);
  EXPECT_EQ(received->payload_size, payload.size());
  EXPECT_EQ(std::string(received->payload, payload.size()), payload);
}

TEST_F(SharedQueueTest, SendReceiveResponse) {
  auto server_queue = SharedQueue::create_server_queue(instrument_name_);
  auto worker_queue = SharedQueue::create_worker_queue(instrument_name_);

  // Worker sends response
  IPCMessage resp_msg;
  resp_msg.type = IPCMessage::Type::RESPONSE;
  resp_msg.id = 99;
  std::string resp_payload = "{\"success\": true}";
  resp_msg.payload_size = resp_payload.size();
  std::memcpy(resp_msg.payload, resp_payload.c_str(), resp_payload.size());

  // Note: worker sends on response queue (which server receives from)
  EXPECT_TRUE(worker_queue->send(resp_msg, std::chrono::milliseconds(1000)));

  auto received = server_queue->receive(std::chrono::milliseconds(1000));
  ASSERT_TRUE(received.has_value());
  EXPECT_EQ(received->type, IPCMessage::Type::RESPONSE);
  EXPECT_EQ(received->id, 99);
}

TEST_F(SharedQueueTest, SendTimeout) {
  auto server_queue = SharedQueue::create_server_queue(instrument_name_);

  // Fill queue with messages
  IPCMessage msg;
  msg.type = IPCMessage::Type::COMMAND;
  msg.id = 1;
  msg.payload_size = 0;

  // Send up to queue limit (100 messages)
  int sent = 0;
  for (int i = 0; i < 105; i++) {
    if (server_queue->send(msg, std::chrono::milliseconds(10))) {
      sent++;
    } else {
      break;
    }
  }

  EXPECT_GT(sent, 0);
  EXPECT_LT(sent, 105); // Should timeout before all sent
}

TEST_F(SharedQueueTest, ReceiveTimeout) {
  auto server_queue = SharedQueue::create_server_queue(instrument_name_);
  auto worker_queue = SharedQueue::create_worker_queue(instrument_name_);

  // Try to receive with no messages
  auto start = std::chrono::steady_clock::now();
  auto received = worker_queue->receive(std::chrono::milliseconds(100));
  auto elapsed = std::chrono::steady_clock::now() - start;

  EXPECT_FALSE(received.has_value());
  EXPECT_GE(elapsed, std::chrono::milliseconds(90)); // Allow some slack
}

TEST_F(SharedQueueTest, MultipleMessages) {
  auto server_queue = SharedQueue::create_server_queue(instrument_name_);
  auto worker_queue = SharedQueue::create_worker_queue(instrument_name_);

  // Send multiple messages
  for (int i = 0; i < 10; i++) {
    IPCMessage msg;
    msg.type = IPCMessage::Type::COMMAND;
    msg.id = i;
    msg.payload_size = 0;
    EXPECT_TRUE(server_queue->send(msg, std::chrono::milliseconds(1000)));
  }

  // Receive in order
  for (int i = 0; i < 10; i++) {
    auto received = worker_queue->receive(std::chrono::milliseconds(1000));
    ASSERT_TRUE(received.has_value());
    EXPECT_EQ(received->id, static_cast<uint64_t>(i));
  }
}

TEST_F(SharedQueueTest, HeartbeatMessage) {
  auto server_queue = SharedQueue::create_server_queue(instrument_name_);
  auto worker_queue = SharedQueue::create_worker_queue(instrument_name_);

  IPCMessage heartbeat;
  heartbeat.type = IPCMessage::Type::HEARTBEAT;
  heartbeat.id = 0;
  heartbeat.payload_size = 0;

  EXPECT_TRUE(worker_queue->send(heartbeat, std::chrono::milliseconds(1000)));

  auto received = server_queue->receive(std::chrono::milliseconds(1000));
  ASSERT_TRUE(received.has_value());
  EXPECT_EQ(received->type, IPCMessage::Type::HEARTBEAT);
}

TEST_F(SharedQueueTest, ShutdownMessage) {
  auto server_queue = SharedQueue::create_server_queue(instrument_name_);
  auto worker_queue = SharedQueue::create_worker_queue(instrument_name_);

  IPCMessage shutdown;
  shutdown.type = IPCMessage::Type::SHUTDOWN;
  shutdown.id = 0;
  shutdown.payload_size = 0;

  EXPECT_TRUE(server_queue->send(shutdown, std::chrono::milliseconds(1000)));

  auto received = worker_queue->receive(std::chrono::milliseconds(1000));
  ASSERT_TRUE(received.has_value());
  EXPECT_EQ(received->type, IPCMessage::Type::SHUTDOWN);
}

TEST_F(SharedQueueTest, LargePayload) {
  auto server_queue = SharedQueue::create_server_queue(instrument_name_);
  auto worker_queue = SharedQueue::create_worker_queue(instrument_name_);

  IPCMessage msg;
  msg.type = IPCMessage::Type::COMMAND;
  msg.id = 1;

  // Fill payload to max
  std::string large_payload(IPCMessage::MAX_PAYLOAD_SIZE - 1, 'X');
  msg.payload_size = large_payload.size();
  std::memcpy(msg.payload, large_payload.c_str(), large_payload.size());

  EXPECT_TRUE(server_queue->send(msg, std::chrono::milliseconds(1000)));

  auto received = worker_queue->receive(std::chrono::milliseconds(1000));
  ASSERT_TRUE(received.has_value());
  EXPECT_EQ(received->payload_size, large_payload.size());
}

class ProcessManagerTest : public ::testing::Test {
protected:
  ProcessManager manager_;
};

TEST_F(ProcessManagerTest, ListProcessesEmpty) {
  auto pids = manager_.list_processes();
  EXPECT_TRUE(pids.empty());
}

TEST_F(ProcessManagerTest, GetNonexistentProcess) {
  auto info = manager_.get_process_info(99999);
  EXPECT_EQ(info, nullptr);
}

TEST_F(ProcessManagerTest, IsAliveNonexistent) {
  EXPECT_FALSE(manager_.is_alive(99999));
}

// Note:  Actual process spawning tests would require a test worker executable
// These are covered in integration tests
