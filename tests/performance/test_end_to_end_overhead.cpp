#include "PlatformPaths.hpp"
#include "instrument-script-server/client/instrument-server-client.hpp"
#include "instrument-script-server/daemon/ServerDaemon.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <thread>

using namespace instserver;
using namespace std::chrono;

#ifndef TEST_DATA_DIR
#define TEST_DATA_DIR "."
#endif

std::string api_path =
    (std::filesystem::path(TEST_DATA_DIR) / "mock_api.yaml").generic_string();

namespace {
uint32_t
run_job_to_completion(instserver::client::InstrumentServerClient &client,
                      const std::string &script_path) {
  namespace v1 = instserver::daemon::v1;
  v1::MeasureJobRequest req;
  req.set_script_path(script_path);

  v1::MeasureJobResponse resp = client.measure_job(req);
  uint32_t job_id = resp.job_id();

  v1::JobStatusRequest status_req;
  status_req.set_job_id(job_id);

  while (true) {
    auto status_resp = client.job_status(status_req);
    auto status = status_resp.job().status();
    if (status == v1::JOB_STATUS_COMPLETED) {
      break;
    }
    if (status == v1::JOB_STATUS_FAILED || status == v1::JOB_STATUS_CANCELLED) {
      auto err = resp.mutable_standard_response()->mutable_error();
      std::string message;
      if (err->code() > 0) {
        message = err->message();
      }
      throw std::runtime_error("Job failed or cancelled: " + message);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return job_id;
}
} // namespace

class EndToEndPerformanceTest : public ::testing::Test {
protected:
  std::unique_ptr<instserver::client::InstrumentServerClient> client;
  uint16_t port = 8556;

  void SetUp() override {
    namespace v1 = instserver::daemon::v1;
    auto &daemon = ServerDaemon::instance();
    if (daemon.is_running()) {
      daemon.stop();
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    daemon.set_rpc_port(port);
    ASSERT_TRUE(daemon.start());
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    client = std::make_unique<instserver::client::InstrumentServerClient>(port);

    // Register / Start MockInstrument1
    v1::StartInstrumentRequest req;
    std::string config_path =
        (std::filesystem::path(TEST_DATA_DIR) / "mock_instrument1.yaml")
            .generic_string();
    req.set_config_path(config_path);
    std::filesystem::path plugin_path =
        instserver::test::get_test_plugin_path("mock_visa_plugin");
    req.set_plugin_path(plugin_path.generic_string());
    req.set_log_level("info");

    try {
      client->start_instrument(req);
    } catch (const std::exception &e) {
      daemon.stop();
      FAIL() << "Failed to start MockInstrument1: " << e.what();
    }
  }

  void TearDown() override {
    namespace v1 = instserver::daemon::v1;
    if (client) {
      v1::StopInstrumentRequest stop_req;
      stop_req.set_instrument_name("MockInstrument1");
      try {
        client->stop_instrument(stop_req);
      } catch (...) {
      }
    }
    client.reset();
    auto &daemon = ServerDaemon::instance();
    if (daemon.is_running()) {
      daemon.stop();
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
  }
};

TEST_F(EndToEndPerformanceTest, SingleCommandOverhead) {
  // Measure best-case overhead: single simple command with no data
  std::filesystem::path temp_dir =
      std::filesystem::temp_directory_path() / "perf_test";
  std::filesystem::create_directories(temp_dir);
  std::filesystem::path script_path = temp_dir / "single_command.lua";
  {
    std::ofstream ofs(script_path);
    ofs << R"(
      -- Warm up
      for i = 1, 10 do
        context:call('MockInstrument1.IDN')
      end
    )";
    ofs.close();
  }

  // Warm up
  run_job_to_completion(*client, script_path.generic_string());

  const int num_calls = 100000;
  {
    std::ofstream ofs(script_path);
    ofs << "for i = 1, " << num_calls << " do\n";
    ofs << "  context:call('MockInstrument1.IDN')\n";
    ofs << "end\n";
    ofs.close();
  }

  auto start = high_resolution_clock::now();
  run_job_to_completion(*client, script_path.generic_string());
  auto end = high_resolution_clock::now();

  auto duration = duration_cast<microseconds>(end - start);

  long avg_latency = duration.count() / (long)num_calls;
  long calls_per_sec = (long)(num_calls * 1000000.0) / (long)duration.count();

  std::cout << "\n=== Single Command Overhead (Best Case) ===\n";
  std::cout << "Average latency per command: " << avg_latency << " µs\n";
  std::cout << "Throughput: " << calls_per_sec << " commands/sec\n";
  std::cout << "Total time for " << num_calls
            << " calls: " << duration.count() / (long)1000.0 << " ms\n";

  std::filesystem::remove(script_path);

  // Overhead should be reasonable (less than 5ms per command on average)
  EXPECT_LT(avg_latency, 5000.0);
}

TEST_F(EndToEndPerformanceTest, CommandWithParametersOverhead) {
  // Measure overhead with command parameters (more realistic case)
  std::filesystem::path temp_dir =
      std::filesystem::temp_directory_path() / "perf_test";
  std::filesystem::create_directories(temp_dir);
  std::filesystem::path script_path = temp_dir / "param_command.lua";
  const int num_calls = 100000;
  {
    std::ofstream ofs(script_path);
    ofs << "for i = 1, " << num_calls << " do\n";
    ofs << "  context:call('MockInstrument1.SET', {voltage = 5.0})\n";
    ofs << "end\n";
    ofs.close();
  }

  auto start = high_resolution_clock::now();
  run_job_to_completion(*client, script_path.generic_string());
  auto end = high_resolution_clock::now();

  auto duration = duration_cast<microseconds>(end - start);

  long avg_latency = (long)duration.count() / (long)num_calls;

  std::cout << "\n=== Command with Parameters Overhead ===\n";
  std::cout << "Average latency per command: " << avg_latency << " µs\n";
  std::cout << "Throughput: "
            << ((long)num_calls * (long)1000000.0) / (long)duration.count()
            << " commands/sec\n";

  std::filesystem::remove(script_path);
}

TEST_F(EndToEndPerformanceTest, MaxConcurrentInstruments) {
  // Test maximum number of concurrent instruments
  namespace v1 = instserver::daemon::v1;
  std::vector<std::string> instrument_names;
  const int max_instruments = 10;

  auto start_setup = high_resolution_clock::now();
  std::filesystem::path temp_dir =
      std::filesystem::temp_directory_path() / "perf_test";
  std::filesystem::create_directories(temp_dir);

  for (int i = 2; i <= max_instruments; i++) {
    std::string config = R"(
name: MockInstrument)" + std::to_string(i) +
                         R"(
api_ref: )" + api_path + R"(
connection:
  type: VISA
  address: "mock://test)" +
                         std::to_string(i) + R"("
)";

    std::string config_path =
        (temp_dir / ("mock_instrument_" + std::to_string(i) + ".yaml"))
            .generic_string();
    std::ofstream config_file(config_path);
    config_file << config;
    config_file.close();

    try {
      v1::StartInstrumentRequest req;
      req.set_config_path(config_path);
      req.set_plugin_path(
          instserver::test::get_test_plugin_path("mock_visa_plugin")
              .generic_string());
      req.set_log_level("info");
      client->start_instrument(req);
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
  const int calls_per_instrument = 1000;
  std::filesystem::path script_path = temp_dir / "concurrent_instruments.lua";
  {
    std::ofstream ofs(script_path);
    ofs << "for i = 1, " << calls_per_instrument << " do\n";
    ofs << "  context:call('MockInstrument1.IDN')\n";
    for (const auto &name : instrument_names) {
      ofs << "  context:call('" << name << ".IDN')\n";
    }
    ofs << "end\n";
    ofs.close();
  }

  auto start_exec = high_resolution_clock::now();
  run_job_to_completion(*client, script_path.generic_string());
  auto end_exec = high_resolution_clock::now();
  auto exec_duration = duration_cast<milliseconds>(end_exec - start_exec);

  int total_calls = calls_per_instrument * (instrument_names.size() + 1);

  std::cout << "\n=== Maximum Concurrent Instruments Test ===\n";
  std::cout << "Number of instruments: " << (instrument_names.size() + 1)
            << "\n";
  std::cout << "Setup time: " << setup_duration.count() << " ms\n";
  std::cout << "Execution time for " << total_calls
            << " calls: " << exec_duration.count() << " ms\n";
  std::cout << "Average latency per call: "
            << (long)(exec_duration.count() * (long)1000.0) / (long)total_calls
            << " µs\n";

  // Clean up
  for (const auto &name : instrument_names) {
    v1::StopInstrumentRequest stop_req;
    stop_req.set_instrument_name(name);
    try {
      client->stop_instrument(stop_req);
    } catch (...) {
    }
  }
  std::filesystem::remove_all(temp_dir);
}

TEST_F(EndToEndPerformanceTest, ParallelExecutionOverhead) {
  // Measure overhead of parallel execution coordination
  namespace v1 = instserver::daemon::v1;
  std::filesystem::path temp_dir =
      std::filesystem::temp_directory_path() / "perf_test";
  std::filesystem::create_directories(temp_dir);

  // Create second instrument
  std::string config2 = R"(
name: MockInstrument2
api_ref: )" + api_path + R"(
connection:
  type: VISA
  address: "mock://test2"
)";

  std::string config_path =
      (temp_dir / "mock_instrument_2.yaml").generic_string();
  std::ofstream config_file(config_path);
  config_file << config2;
  config_file.close();

  v1::StartInstrumentRequest req;
  req.set_config_path(config_path);
  req.set_plugin_path(instserver::test::get_test_plugin_path("mock_visa_plugin")
                          .generic_string());
  req.set_log_level("info");
  client->start_instrument(req);

  const int num_parallel_blocks = 10000;
  std::filesystem::path script_path = temp_dir / "parallel_exec.lua";
  {
    std::ofstream ofs(script_path);
    ofs << "for i = 1, " << num_parallel_blocks << " do\n";
    ofs << R"(
      context:parallel(function()
        context:call('MockInstrument1.IDN')
        context:call('MockInstrument2.IDN')
      end)
    )";
    ofs << "end\n";
    ofs.close();
  }

  auto start = high_resolution_clock::now();
  run_job_to_completion(*client, script_path.generic_string());
  auto end = high_resolution_clock::now();
  auto duration = duration_cast<microseconds>(end - start);

  long avg_latency = duration.count() / (long)num_parallel_blocks;

  std::cout << "\n=== Parallel Execution Overhead ===\n";
  std::cout << "Average latency per parallel block (2 commands): "
            << avg_latency << " µs\n";
  std::cout << "Throughput: "
            << (long)(num_parallel_blocks * 1000000.0) / duration.count()
            << " parallel blocks/sec\n";
  std::cout << "Total time for " << num_parallel_blocks
            << " parallel blocks: " << (long)duration.count() / (long)1000.0
            << " ms\n";

  // Clean up
  v1::StopInstrumentRequest stop_req;
  stop_req.set_instrument_name("MockInstrument2");
  try {
    client->stop_instrument(stop_req);
  } catch (...) {
  }
  std::filesystem::remove_all(temp_dir);
}
