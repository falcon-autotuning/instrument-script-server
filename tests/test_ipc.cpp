#include "instrument-server/ipc/SharedQueue.hpp"
#include "instrument-server/ipc/WorkerProtocol.hpp"
#include <gtest/gtest.h>

using namespace instserver;
using namespace instserver::ipc;

TEST(IPCTest, QueueCreation) {
  // Create server queue
  auto server_queue = SharedQueue::create_server_queue("test_instrument");
  EXPECT_TRUE(server_queue->is_valid());

  // Create worker queue
  auto worker_queue = SharedQueue::create_worker_queue("test_instrument");
  EXPECT_TRUE(worker_queue->is_valid());

  // Cleanup
  SharedQueue::cleanup("test_instrument");
}

TEST(IPCTest, MessageSendReceive) {
  auto server_queue = SharedQueue::create_server_queue("test_msg");
  auto worker_queue = SharedQueue::create_worker_queue("test_msg");

  // Send message from server
  IPCMessage msg;
  msg.type = IPCMessage::Type::COMMAND;
  msg.id = 42;
  msg.payload_size = 10;
  std::memcpy(msg.payload, "test_data", 10);

  EXPECT_TRUE(server_queue->send(msg, std::chrono::milliseconds(1000)));

  // Receive on worker side
  auto received = worker_queue->receive(std::chrono::milliseconds(1000));
  ASSERT_TRUE(received.has_value());
  EXPECT_EQ(received->type, IPCMessage::Type::COMMAND);
  EXPECT_EQ(received->id, 42);
  EXPECT_EQ(received->payload_size, 10);
  EXPECT_EQ(std::string(received->payload, 10), "test_data");

  SharedQueue::cleanup("test_msg");
}

TEST(IPCTest, CommandSerialization) {
  SerializedCommand cmd;
  cmd.id = "test-123";
  cmd.instrument_name = "DMM1";
  cmd.verb = "MEASURE_VOLTAGE";
  cmd.timeout = std::chrono::milliseconds(5000);
  cmd.expects_response = true;
  cmd.params["range"] = 10.0;
  cmd.params["samples"] = static_cast<int64_t>(100);

  std::string serialized = serialize_command(cmd);
  EXPECT_FALSE(serialized.empty());

  SerializedCommand deserialized = deserialize_command(serialized);
  EXPECT_EQ(deserialized.id, cmd.id);
  EXPECT_EQ(deserialized.instrument_name, cmd.instrument_name);
  EXPECT_EQ(deserialized.verb, cmd.verb);
  EXPECT_EQ(deserialized.expects_response, cmd.expects_response);
  EXPECT_DOUBLE_EQ(std::get<double>(deserialized.params["range"]), 10.0);
  EXPECT_EQ(std::get<int64_t>(deserialized.params["samples"]), 100);
}

TEST(IPCTest, ResponseSerialization) {
  CommandResponse resp;
  resp.command_id = "test-456";
  resp.instrument_name = "SCOPE1";
  resp.success = true;
  resp.text_response = "3.142";
  resp.return_value = 3.142;

  std::string serialized = serialize_response(resp);
  EXPECT_FALSE(serialized.empty());

  CommandResponse deserialized = deserialize_response(serialized);
  EXPECT_EQ(deserialized.command_id, resp.command_id);
  EXPECT_EQ(deserialized.instrument_name, resp.instrument_name);
  EXPECT_EQ(deserialized.success, resp.success);
  EXPECT_EQ(deserialized.text_response, resp.text_response);
  EXPECT_TRUE(deserialized.return_value.has_value());
  EXPECT_DOUBLE_EQ(std::get<double>(*deserialized.return_value), 3.142);
}
