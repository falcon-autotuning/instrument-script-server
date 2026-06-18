#include "instrument-script-server/ipc/SharedQueue.hpp"

#include <chrono>
#include <gtest/gtest.h>
#include <thread>
namespace {
void copy_string(char *dst, size_t dst_size, const std::string &src) {
  std::strncpy(dst, src.c_str(), dst_size - 1);
  dst[dst_size - 1] = '\0';
}
} // namespace
using namespace instserver::ipc;

TEST(IPCPerformance, Throughput) {
  std::string name = "perf_queue";
  auto server_queue = SharedQueue::create_server_queue(name);
  auto worker_queue = SharedQueue::create_worker_queue(name);

  const int num_messages = 100000;

  auto start = std::chrono::high_resolution_clock::now();

  // Sender thread
  std::thread sender([&]() {
    for (int i = 0; i < num_messages; i++) {
      IPCMessage msg;
      msg.type = IPCMessage::Type::COMMAND;
      copy_string(msg.id.data(), msg.id.size(), std::to_string(i));
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
