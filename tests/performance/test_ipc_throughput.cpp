#include "instrument-server/ipc/SharedQueue.hpp"
#include <chrono>
#include <gtest/gtest.h>
#include <thread>

using namespace instserver::ipc;

TEST(IPCPerformance, Throughput) {
  std::string name = "perf_queue";
  auto server_queue = SharedQueue::create_server_queue(name);
  auto worker_queue = SharedQueue::create_worker_queue(name);

  const int num_messages = 1000;

  auto start = std::chrono::high_resolution_clock::now();

  // Sender thread
  std::thread sender([&]() {
    for (int i = 0; i < num_messages; i++) {
      IPCMessage msg;
      msg.type = IPCMessage::Type::COMMAND;
      msg.id = i;
      msg.payload_size = 0;
      server_queue->send(msg, std::chrono::seconds(1));
    }
  });

  // Receiver thread
  std::thread receiver([&]() {
    for (int i = 0; i < num_messages; i++) {
      auto msg = worker_queue->receive(std::chrono::seconds(1));
      ASSERT_TRUE(msg.has_value());
    }
  });

  sender.join();
  receiver.join();

  auto end = std::chrono::high_resolution_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::microseconds>(end - start);

  double messages_per_sec = (num_messages * 1000000.0) / duration.count();

  std::cout << "IPC Throughput: " << messages_per_sec << " msg/s\n";
  std::cout << "Average latency: " << (duration.count() / num_messages)
            << " µs\n";

  SharedQueue::cleanup(name);
}
