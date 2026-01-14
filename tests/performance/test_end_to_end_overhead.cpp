#include "PluginTestFixture.hpp"
#include "instrument-server/server/InstrumentRegistry.hpp"
#include "instrument-server/server/RuntimeContext.hpp"
#include "instrument-server/server/ServerDaemon.hpp"
#include "instrument-server/server/SyncCoordinator.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <sol/sol.hpp>

using namespace instserver;
using namespace std::chrono;

class EndToEndPerformanceTest : public test::PluginTestFixture {
protected:
  void SetUp() override {
    PluginTestFixture::SetUp();

    // Start daemon
    auto &daemon = ServerDaemon::instance();
    if (!daemon.is_running()) {
      daemon.start();
    }

    // Start mock instrument
    auto &registry = InstrumentRegistry::instance();
    std::string config_path = "tests/data/mock_instrument1.yaml";
    if (std::filesystem::exists(config_path)) {
      registry.create_instrument(config_path);
    }
  }

  void TearDown() override {
    auto &registry = InstrumentRegistry::instance();
    registry.stop_all();
  }
};

TEST_F(EndToEndPerformanceTest, SingleCommandOverhead) {
  // Measure best-case overhead: single simple command with no data
  auto &registry = InstrumentRegistry::instance();
  SyncCoordinator sync;

  sol::state lua;
  lua.open_libraries(sol::lib::base, sol::lib::math);
  bind_runtime_context(lua, registry, sync);

  RuntimeContext ctx(registry, sync);
  lua["context"] = &ctx;

  // Warm up
  for (int i = 0; i < 10; i++) {
    lua.script("context:call('MockInstrument1.IDN')");
  }

  // Measure
  const int num_calls = 1000;
  auto start = high_resolution_clock::now();

  for (int i = 0; i < num_calls; i++) {
    lua.script("context:call('MockInstrument1.IDN')");
  }

  auto end = high_resolution_clock::now();
  auto duration = duration_cast<microseconds>(end - start);

  double avg_latency = duration.count() / (double)num_calls;
  double calls_per_sec = (num_calls * 1000000.0) / duration.count();

  std::cout << "\n=== Single Command Overhead (Best Case) ===\n";
  std::cout << "Average latency per command: " << avg_latency << " µs\n";
  std::cout << "Throughput: " << calls_per_sec << " commands/sec\n";
  std::cout << "Total time for " << num_calls
            << " calls: " << duration.count() / 1000.0 << " ms\n";

  // Overhead should be reasonable (less than 5ms per command on average)
  EXPECT_LT(avg_latency, 5000.0);
}

TEST_F(EndToEndPerformanceTest, CommandWithParametersOverhead) {
  // Measure overhead with command parameters (more realistic case)
  auto &registry = InstrumentRegistry::instance();
  SyncCoordinator sync;

  sol::state lua;
  lua.open_libraries(sol::lib::base, sol::lib::math);
  bind_runtime_context(lua, registry, sync);

  RuntimeContext ctx(registry, sync);
  lua["context"] = &ctx;

  const int num_calls = 1000;
  auto start = high_resolution_clock::now();

  for (int i = 0; i < num_calls; i++) {
    lua.script("context:call('MockInstrument1.SET', {voltage = 5.0})");
  }

  auto end = high_resolution_clock::now();
  auto duration = duration_cast<microseconds>(end - start);

  double avg_latency = duration.count() / (double)num_calls;

  std::cout << "\n=== Command with Parameters Overhead ===\n";
  std::cout << "Average latency per command: " << avg_latency << " µs\n";
  std::cout << "Throughput: " << (num_calls * 1000000.0) / duration.count()
            << " commands/sec\n";
}

TEST_F(EndToEndPerformanceTest, WorstCaseMaxPayload) {
  // Worst case: large data transfers (if supported)
  auto &registry = InstrumentRegistry::instance();
  SyncCoordinator sync;

  sol::state lua;
  lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::table);
  bind_runtime_context(lua, registry, sync);

  RuntimeContext ctx(registry, sync);
  lua["context"] = &ctx;

  // Test with array return values (heavier payload)
  const int num_calls = 100;
  auto start = high_resolution_clock::now();

  for (int i = 0; i < num_calls; i++) {
    lua.script("context:call('MockInstrument1.GET_ARRAY')");
  }

  auto end = high_resolution_clock::now();
  auto duration = duration_cast<microseconds>(end - start);

  double avg_latency = duration.count() / (double)num_calls;

  std::cout << "\n=== Worst Case: Array/Large Data Overhead ===\n";
  std::cout << "Average latency per command: " << avg_latency << " µs\n";
  std::cout << "Throughput: " << (num_calls * 1000000.0) / duration.count()
            << " commands/sec\n";
}

TEST_F(EndToEndPerformanceTest, MaxConcurrentInstruments) {
  // Test maximum number of concurrent instruments
  auto &registry = InstrumentRegistry::instance();

  // Create multiple mock instruments
  std::vector<std::string> instrument_names;
  const int max_instruments = 10;

  auto start_setup = high_resolution_clock::now();

  for (int i = 2; i <= max_instruments; i++) {
    std::string config = R"(
name: MockInstrument)" + std::to_string(i) +
                         R"(
api_ref: tests/data/mock_api.yaml
connection:
  type: VISA
  address: "mock://test)" +
                         std::to_string(i) + R"("
)";

    std::string config_path =
        "/tmp/mock_instrument_" + std::to_string(i) + ".yaml";
    std::ofstream config_file(config_path);
    config_file << config;
    config_file.close();

    try {
      registry.create_instrument(config_path);
      instrument_names.push_back("MockInstrument" + std::to_string(i));
    } catch (const std::exception &e) {
      std::cout << "Failed to create instrument " << i << ": " << e.what()
                << "\n";
      break;
    }
  }

  auto end_setup = high_resolution_clock::now();
  auto setup_duration = duration_cast<milliseconds>(end_setup - start_setup);

  // Now execute commands across all instruments
  SyncCoordinator sync;
  sol::state lua;
  lua.open_libraries(sol::lib::base);
  bind_runtime_context(lua, registry, sync);

  RuntimeContext ctx(registry, sync);
  lua["context"] = &ctx;

  const int calls_per_instrument = 100;
  auto start_exec = high_resolution_clock::now();

  for (int i = 0; i < calls_per_instrument; i++) {
    for (const auto &name : instrument_names) {
      std::string cmd = "context:call('" + name + ".IDN')";
      lua.script(cmd);
    }
  }

  auto end_exec = high_resolution_clock::now();
  auto exec_duration = duration_cast<milliseconds>(end_exec - start_exec);

  std::cout << "\n=== Maximum Concurrent Instruments Test ===\n";
  std::cout << "Number of instruments: " << (instrument_names.size() + 1)
            << "\n";
  std::cout << "Setup time: " << setup_duration.count() << " ms\n";
  std::cout << "Execution time for "
            << (calls_per_instrument * (instrument_names.size() + 1))
            << " calls: " << exec_duration.count() << " ms\n";
  std::cout << "Average latency per call: "
            << (exec_duration.count() * 1000.0) /
                   (calls_per_instrument * (instrument_names.size() + 1))
            << " µs\n";

  // Clean up
  for (size_t i = 0; i < instrument_names.size(); i++) {
    registry.remove_instrument(instrument_names[i]);
    std::remove(
        ("/tmp/mock_instrument_" + std::to_string(i + 2) + ".yaml").c_str());
  }
}

TEST_F(EndToEndPerformanceTest, DISABLED_ParallelExecutionOverhead) {
  // Measure overhead of parallel execution coordination
  auto &registry = InstrumentRegistry::instance();

  // Create second instrument
  std::string config2 = R"(
name: MockInstrument2
api_ref: tests/data/mock_api.yaml
connection:
  type: VISA
  address: "mock://test2"
)";

  std::string config_path = "/tmp/mock_instrument_2.yaml";
  std::ofstream config_file(config_path);
  config_file << config2;
  config_file.close();
  registry.create_instrument(config_path);

  SyncCoordinator sync;
  sol::state lua;
  lua.open_libraries(sol::lib::base);
  bind_runtime_context(lua, registry, sync);

  RuntimeContext ctx(registry, sync);
  lua["context"] = &ctx;

  const int num_parallel_blocks = 100;
  auto start = high_resolution_clock::now();

  for (int i = 0; i < num_parallel_blocks; i++) {
    lua.script(R"(
      context:parallel_start()
      context:call('MockInstrument1.IDN')
      context:call('MockInstrument2.IDN')
      context:parallel_end()
    )");
  }

  auto end = high_resolution_clock::now();
  auto duration = duration_cast<microseconds>(end - start);

  double avg_latency = duration.count() / (double)num_parallel_blocks;

  std::cout << "\n=== Parallel Execution Overhead ===\n";
  std::cout << "Average latency per parallel block (2 commands): "
            << avg_latency << " µs\n";
  std::cout << "Throughput: "
            << (num_parallel_blocks * 1000000.0) / duration.count()
            << " parallel blocks/sec\n";
  std::cout << "Total time for " << num_parallel_blocks
            << " parallel blocks: " << duration.count() / 1000.0 << " ms\n";

  // Clean up
  registry.remove_instrument("MockInstrument2");
  std::remove(config_path.c_str());
}
