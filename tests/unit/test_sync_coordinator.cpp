#include "instrument-server/server/SyncCoordinator.hpp"

#include <algorithm>
#include <gtest/gtest.h>

using namespace instserver;

TEST(SyncCoordinator, RegisterBarrier) {
  SyncCoordinator sync;

  std::vector<std::string> instruments = {"DAC1", "DAC2", "DMM1"};
  sync.register_barrier(1, instruments);

  EXPECT_TRUE(sync.has_barrier(1));
  EXPECT_FALSE(sync.has_barrier(2));
}

TEST(SyncCoordinator, HandleAck_PartialCompletion) {
  SyncCoordinator sync;

  std::vector<std::string> instruments = {"DAC1", "DAC2", "DMM1"};
  sync.register_barrier(1, instruments);

  EXPECT_FALSE(sync.handle_ack(1, "DAC1"));
  EXPECT_FALSE(sync.handle_ack(1, "DAC2"));
  EXPECT_TRUE(sync.handle_ack(1, "DMM1")); // Last one completes
}

TEST(SyncCoordinator, HandleAck_AllComplete) {
  SyncCoordinator sync;

  std::vector<std::string> instruments = {"DAC1", "DAC2"};
  sync.register_barrier(42, instruments);

  bool complete1 = sync.handle_ack(42, "DAC1");
  EXPECT_FALSE(complete1);

  bool complete2 = sync.handle_ack(42, "DAC2");
  EXPECT_TRUE(complete2);
}

TEST(SyncCoordinator, GetWaitingInstruments) {
  SyncCoordinator sync;

  std::vector<std::string> instruments = {"DAC1", "DAC2", "DMM1"};
  sync.register_barrier(1, instruments);

  sync.handle_ack(1, "DAC1");

  auto waiting = sync.get_waiting_instruments(1);
  EXPECT_EQ(waiting.size(), 2);
  EXPECT_TRUE(std::find(waiting.begin(), waiting.end(), "DAC2") !=
              waiting.end());
  EXPECT_TRUE(std::find(waiting.begin(), waiting.end(), "DMM1") !=
              waiting.end());
}

TEST(SyncCoordinator, ClearBarrier) {
  SyncCoordinator sync;

  std::vector<std::string> instruments = {"DAC1"};
  sync.register_barrier(1, instruments);

  EXPECT_TRUE(sync.has_barrier(1));
  sync.clear_barrier(1);
  EXPECT_FALSE(sync.has_barrier(1));
}

TEST(SyncCoordinator, MultipleBarriers) {
  SyncCoordinator sync;

  sync.register_barrier(1, {"DAC1", "DAC2"});
  sync.register_barrier(2, {"DMM1", "Scope1"});
  sync.register_barrier(3, {"DAC1", "DMM1"});

  EXPECT_EQ(sync.active_barrier_count(), 3);

  sync.handle_ack(1, "DAC1");
  sync.handle_ack(1, "DAC2");

  sync.clear_barrier(1);
  EXPECT_EQ(sync.active_barrier_count(), 2);
}

TEST(SyncCoordinator, DuplicateAck) {
  SyncCoordinator sync;

  sync.register_barrier(1, {"DAC1", "DAC2"});

  EXPECT_FALSE(sync.handle_ack(1, "DAC1"));
  EXPECT_FALSE(sync.handle_ack(1, "DAC1")); // Duplicate - ignored
  EXPECT_TRUE(sync.handle_ack(1, "DAC2"));
}

TEST(SyncCoordinator, UnknownToken) {
  SyncCoordinator sync;

  // ACK for non-existent barrier
  EXPECT_FALSE(sync.handle_ack(999, "DAC1"));
}

TEST(SyncCoordinator, UnexpectedInstrument) {
  SyncCoordinator sync;

  sync.register_barrier(1, {"DAC1", "DAC2"});

  // ACK from instrument not in barrier
  EXPECT_FALSE(sync.handle_ack(1, "DMM1"));
}
