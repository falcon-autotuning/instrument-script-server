#include "instrument-server/Logger.hpp"
#include "instrument-server/server/SyncCoordinator.hpp"
#include <algorithm>
#include <gtest/gtest.h>
#include <spdlog/common.h>
#include <thread>

using namespace instserver;

class ParallelExecutionTest : public ::testing::Test {
protected:
  void SetUp() override {
    InstrumentLogger::instance().init("parallel_test.log",
                                      spdlog::level::debug);
    sync_ = std::make_unique<SyncCoordinator>();
  }

  std::unique_ptr<SyncCoordinator> sync_;
};

TEST_F(ParallelExecutionTest, BasicSynchronization) {
  std::vector<std::string> instruments = {"Inst1", "Inst2", "Inst3"};
  uint64_t token = 42;

  sync_->register_barrier(token, instruments);

  // Simulate ACKs from worker threads
  std::vector<std::thread> threads;
  std::atomic<bool> barrier_complete{false};

  for (const auto &inst : instruments) {
    threads.emplace_back([&, inst]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      bool complete = sync_->handle_ack(token, inst);
      if (complete) {
        barrier_complete = true;
      }
    });
  }

  for (auto &t : threads) {
    t.join();
  }

  EXPECT_TRUE(barrier_complete.load());
}

TEST_F(ParallelExecutionTest, OrderIndependence) {
  std::vector<std::string> instruments = {"A", "B", "C", "D"};
  uint64_t token = 1;

  sync_->register_barrier(token, instruments);

  // ACK in random order
  EXPECT_FALSE(sync_->handle_ack(token, "C"));
  EXPECT_FALSE(sync_->handle_ack(token, "A"));
  EXPECT_FALSE(sync_->handle_ack(token, "D"));
  EXPECT_TRUE(sync_->handle_ack(token, "B")); // Last one
}

TEST_F(ParallelExecutionTest, MultipleBarriersSimultaneous) {
  sync_->register_barrier(1, {"A", "B"});
  sync_->register_barrier(2, {"C", "D"});
  sync_->register_barrier(3, {"E", "F"});

  // Complete barrier 2
  EXPECT_FALSE(sync_->handle_ack(2, "C"));
  EXPECT_TRUE(sync_->handle_ack(2, "D"));

  // Barrier 1 and 3 still active
  EXPECT_TRUE(sync_->has_barrier(1));
  EXPECT_FALSE(sync_->has_barrier(2)); // Should be cleared
  EXPECT_TRUE(sync_->has_barrier(3));
}

TEST_F(ParallelExecutionTest, WaitingInstruments) {
  sync_->register_barrier(1, {"A", "B", "C", "D", "E"});

  sync_->handle_ack(1, "A");
  sync_->handle_ack(1, "C");
  sync_->handle_ack(1, "E");

  auto waiting = sync_->get_waiting_instruments(1);
  EXPECT_EQ(waiting.size(), 2);
  EXPECT_TRUE(std::find(waiting.begin(), waiting.end(), "B") != waiting.end());
  EXPECT_TRUE(std::find(waiting.begin(), waiting.end(), "D") != waiting.end());
}

TEST_F(ParallelExecutionTest, HighLoadConcurrency) {
  // Stress test with many barriers and concurrent ACKs
  const int num_barriers = 100;
  const int instruments_per_barrier = 5;

  std::vector<std::thread> threads;

  // Register many barriers
  for (int i = 0; i < num_barriers; i++) {
    std::vector<std::string> instruments;
    for (int j = 0; j < instruments_per_barrier; j++) {
      instruments.push_back("Inst_" + std::to_string(i) + "_" +
                            std::to_string(j));
    }
    sync_->register_barrier(i, instruments);
  }

  // ACK all concurrently
  for (int i = 0; i < num_barriers; i++) {
    for (int j = 0; j < instruments_per_barrier; j++) {
      threads.emplace_back([&, i, j]() {
        std::string inst =
            "Inst_" + std::to_string(i) + "_" + std::to_string(j);
        sync_->handle_ack(i, inst);
      });
    }
  }

  for (auto &t : threads) {
    t.join();
  }

  // All barriers should be complete
  for (int i = 0; i < num_barriers; i++) {
    auto waiting = sync_->get_waiting_instruments(i);
    EXPECT_EQ(waiting.size(), 0) << "Barrier " << i << " not complete";
  }
}
