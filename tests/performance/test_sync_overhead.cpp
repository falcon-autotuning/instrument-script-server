#include "instrument-server/server/SyncCoordinator.hpp"
#include <chrono>
#include <gtest/gtest.h>

using namespace instserver;

TEST(SyncPerformance, BarrierOverhead) {
  SyncCoordinator sync;

  const int num_iterations = 10000;
  const int instruments_per_barrier = 5;

  auto start = std::chrono::high_resolution_clock::now();

  for (int i = 0; i < num_iterations; i++) {
    std::vector<std::string> instruments;
    for (int j = 0; j < instruments_per_barrier; j++) {
      instruments.push_back("Inst" + std::to_string(j));
    }

    sync.register_barrier(i, instruments);

    for (int j = 0; j < instruments_per_barrier; j++) {
      sync.handle_ack(i, "Inst" + std::to_string(j));
    }

    sync.clear_barrier(i);
  }

  auto end = std::chrono::high_resolution_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::microseconds>(end - start);

  double overhead_per_barrier = duration.count() / (double)num_iterations;

  std::cout << "Sync barrier overhead: " << overhead_per_barrier << " µs\n";

  // Should be less than 100 µs per barrier
  EXPECT_LT(overhead_per_barrier, 100.0);
}
