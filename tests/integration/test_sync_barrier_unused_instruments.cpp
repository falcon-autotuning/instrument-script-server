#include "instrument-server/server/InstrumentRegistry.hpp"
#include "instrument-server/server/JobManager.hpp"
#include "instrument-server/server/ServerDaemon.hpp"
#include <chrono>
#include <fstream>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <thread>

using json = nlohmann::json;
using namespace instserver;

static bool wait_for_job_complete(const std::string &job_id,
                                  int timeout_ms = 5000) {
  auto start = std::chrono::steady_clock::now();
  while (true) {
    instserver::server::JobInfo info;
    if (instserver::server::JobManager::instance().get_job_info(job_id, info)) {
      if (info.status == "completed" || info.status == "failed" ||
          info.status == "canceled")
        return info.status == "completed";
    }
    if (std::chrono::steady_clock::now() - start >
        std::chrono::milliseconds(timeout_ms))
      return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
}

TEST(SyncBarrierTest, UnusedInstrumentsReceiveBarrierNOP) {
  // Start daemon (if not running)
  auto &daemon = ServerDaemon::instance();
  daemon.set_rpc_port(0); // not used
  ASSERT_TRUE(daemon.start());

  auto &registry = InstrumentRegistry::instance();

  // Create two mock instruments via test data config files
  std::filesystem::path cfg1 = std::filesystem::current_path() / "tests" /
                               "data" / "mock_instrument1.yaml";
  std::filesystem::path cfg2 = std::filesystem::current_path() / "tests" /
                               "data" / "mock_instrument2.yaml";

  ASSERT_TRUE(std::filesystem::exists(cfg1));
  ASSERT_TRUE(std::filesystem::exists(cfg2));

  ASSERT_TRUE(registry.create_instrument(cfg1.string()));
  ASSERT_TRUE(registry.create_instrument(cfg2.string()));

  // Prepare a tiny measurement script that only calls instrument1 in a parallel
  // block
  std::string script_path =
      (std::filesystem::current_path() / "tests" / "tmp_measure_only_inst1.lua")
          .string();
  {
    std::ofstream f(script_path);
    f << "context:parallel(function()\n";
    f << "  context:call(\"MockInstrument1.MEASURE\")\n";
    f << "end)\n";
  }

  // Submit measure job (enqueue-first)
  json params;
  params["script_path"] = script_path;
  std::string job_id =
      instserver::server::JobManager::instance().submit_measure(script_path,
                                                                params);
  ASSERT_FALSE(job_id.empty());

  // Wait for completion
  ASSERT_TRUE(wait_for_job_complete(job_id, 10000));

  // After job completion, both instruments should have recorded commands_sent >
  // 0
  auto p1 = registry.get_instrument("MockInstrument1");
  auto p2 = registry.get_instrument("MockInstrument2");
  ASSERT_NE(p1, nullptr);
  ASSERT_NE(p2, nullptr);

  auto s1 = p1->get_stats();
  auto s2 = p2->get_stats();

  EXPECT_GT(s1.commands_sent, 0u);
  EXPECT_GT(s2.commands_sent, 0u);

  // Cleanup
  registry.remove_instrument("MockInstrument1");
  registry.remove_instrument("MockInstrument2");
}
