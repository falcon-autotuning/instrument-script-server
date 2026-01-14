#include "instrument-server/ipc/SharedQueue.hpp"

#include <gtest/gtest.h>
#include <thread>

using namespace instserver::ipc;

TEST(IPCQueue, CreateAndDestroy) {
  std::string name = "test_queue_1";

  auto server_queue = SharedQueue::create_server_queue(name);
  ASSERT_NE(server_queue, nullptr);
  EXPECT_TRUE(server_queue->is_valid());

  server_queue.reset();
  SharedQueue::cleanup(name);
}

TEST(IPCQueue, SendReceive) {
  std::string name = "test_queue_2";

  auto server_queue = SharedQueue::create_server_queue(name);
  auto worker_queue = SharedQueue::create_worker_queue(name);

  ASSERT_NE(server_queue, nullptr);
  ASSERT_NE(worker_queue, nullptr);

  // Send from server
  IPCMessage msg;
  msg.type = IPCMessage::Type::COMMAND;
  msg.id = 42;
  msg.sync_token = 0;
  msg.payload_size = 5;
  std::memcpy(msg.payload, "test", 5);

  bool sent = server_queue->send(msg, std::chrono::milliseconds(1000));
  ASSERT_TRUE(sent);

  // Receive on worker
  auto received = worker_queue->receive(std::chrono::milliseconds(1000));
  ASSERT_TRUE(received.has_value());
  EXPECT_EQ(received->type, IPCMessage::Type::COMMAND);
  EXPECT_EQ(received->id, 42);
  EXPECT_EQ(std::string(received->payload), "test");

  SharedQueue::cleanup(name);
}

TEST(IPCQueue, Timeout) {
  std::string name = "test_queue_3";

  auto queue = SharedQueue::create_server_queue(name);
  ASSERT_NE(queue, nullptr);

  // Try to receive with no messages - should timeout
  auto msg = queue->receive(std::chrono::milliseconds(100));
  EXPECT_FALSE(msg.has_value());

  SharedQueue::cleanup(name);
}
