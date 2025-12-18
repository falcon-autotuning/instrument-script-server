#include "instrument-server/ipc/SharedQueue.hpp"
#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <iostream>
#include <thread>

using namespace instserver::ipc;

class IPCBenchmark : public ::testing::Test {
protected:
  void SetUp() override {
    instrument_name_ = "benchmark_" + std::to_string(std::time(nullptr));
  }

  void TearDown() override { SharedQueue::cleanup(instrument_name_); }

  std::string instrument_name_;

  // Helper to measure throughput
  struct BenchmarkResult {
    int messages_sent;
    int messages_received;
    std::chrono::microseconds duration;
    double messages_per_second;
    double avg_latency_us;
  };

  BenchmarkResult run_throughput_test(int message_count, int payload_size) {
    auto server_queue = SharedQueue::create_server_queue(instrument_name_);
    auto worker_queue = SharedQueue::create_worker_queue(instrument_name_);

    std::atomic<int> received{0};
    std::atomic<bool> sender_done{false};

    auto start = std::chrono::high_resolution_clock::now();

    // Receiver thread - keep running until all messages received OR sender done
    // + timeout
    std::thread receiver([&]() {
      auto last_receive = std::chrono::steady_clock::now();
      while (received < message_count) {
        auto msg = worker_queue->receive(std::chrono::milliseconds(100));
        if (msg) {
          received++;
          last_receive = std::chrono::steady_clock::now();
        }

        // If sender is done and no messages for 200ms, exit
        if (sender_done) {
          auto elapsed = std::chrono::steady_clock::now() - last_receive;
          if (elapsed > std::chrono::milliseconds(200)) {
            break;
          }
        }
      }
    });

    // Sender thread
    int sent = 0;
    for (int i = 0; i < message_count; i++) {
      IPCMessage msg;
      msg.type = IPCMessage::Type::COMMAND;
      msg.id = i;
      msg.payload_size =
          std::min(payload_size, (int)IPCMessage::MAX_PAYLOAD_SIZE);

      // Retry send with backoff if queue full
      int retries = 0;
      while (!server_queue->send(msg, std::chrono::milliseconds(10))) {
        std::this_thread::sleep_for(
            std::chrono::microseconds(100 * (1 << retries)));
        retries++;
        if (retries > 5)
          break; // Give up after exponential backoff
      }

      if (retries <= 5) {
        sent++;
      }
    }

    sender_done = true;
    receiver.join();

    auto end = std::chrono::high_resolution_clock::now();
    auto duration =
        std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    BenchmarkResult result;
    result.messages_sent = sent;
    result.messages_received = received.load();
    result.duration = duration;
    result.messages_per_second =
        (received.load() * 1000000.0) / duration.count();
    result.avg_latency_us =
        duration.count() / (double)std::max(1, received.load());

    return result;
  }
};

TEST_F(IPCBenchmark, SmallMessages100) {
  auto result = run_throughput_test(100, 64);

  std::cout << "\n=== Small Messages (64 bytes, 100 messages) ===" << std::endl;
  std::cout << "Sent:      " << result.messages_sent << std::endl;
  std::cout << "Received: " << result.messages_received << std::endl;
  std::cout << "Duration: " << result.duration.count() / 1000.0 << " ms"
            << std::endl;
  std::cout << "Throughput: " << result.messages_per_second << " msg/s"
            << std::endl;
  std::cout << "Avg Latency: " << result.avg_latency_us << " µs" << std::endl;

  // Sanity check:  should complete and receive most messages
  EXPECT_GT(result.messages_received, 90);
}

TEST_F(IPCBenchmark, MediumMessages1000) {
  auto result = run_throughput_test(1000, 1024);

  std::cout << "\n=== Medium Messages (1KB, 1000 messages) ===" << std::endl;
  std::cout << "Throughput: " << result.messages_per_second << " msg/s"
            << std::endl;
  std::cout << "Avg Latency: " << result.avg_latency_us << " µs" << std::endl;

  EXPECT_GT(result.messages_received, 950);
}

TEST_F(IPCBenchmark, LargeMessages100) {
  auto result = run_throughput_test(100, 8192);

  std::cout << "\n=== Large Messages (8KB, 100 messages) ===" << std::endl;
  std::cout << "Throughput: " << result.messages_per_second << " msg/s"
            << std::endl;
  std::cout << "Avg Latency: " << result.avg_latency_us << " µs" << std::endl;

  EXPECT_GT(result.messages_received, 90);
}

// Stress test:  find queue capacity
TEST_F(IPCBenchmark, QueueCapacityTest) {
  auto server_queue = SharedQueue::create_server_queue(instrument_name_);

  IPCMessage msg;
  msg.type = IPCMessage::Type::COMMAND;
  msg.payload_size = 0;

  int capacity = 0;
  for (int i = 0; i < 200; i++) {
    msg.id = i;
    if (server_queue->send(msg, std::chrono::milliseconds(1))) {
      capacity++;
    } else {
      break;
    }
  }

  std::cout << "\n=== Queue Capacity ===" << std::endl;
  std::cout << "Max messages before full: " << capacity << std::endl;

  // Should be close to 100 (configured queue size)
  EXPECT_GE(capacity, 95);
  EXPECT_LE(capacity, 105);
}
