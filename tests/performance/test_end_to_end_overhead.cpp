#include "PlatformPaths.hpp"
#include "instrument-script-server/client/instrument-server-client.hpp"
#include "instrument-script-server/daemon/ServerDaemon.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#endif

using namespace instserver;
using namespace instserver::client;
using namespace instserver::daemon;
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
uint16_t get_port() {
  const char *env = std::getenv("INSTRUMENT_SCRIPT_SERVER_RPC_PORT");
  if (env == nullptr) {
    return 8555;
  }
  return static_cast<uint16_t>(std::stoi(env));
}
} // namespace

class EndToEndPerformanceTest : public ::testing::Test {
protected:
  std::unique_ptr<instserver::client::InstrumentServerClient> client;

  void SetUp() override {
    namespace v1 = instserver::daemon::v1;
    try {
      instserver::client::InstrumentServerClient check_client(get_port());
      if (check_client.is_daemon_running()) {
        std::cerr << "Daemon is already running on port " << get_port()
                  << ". Stopping it...\n";
        instserver::client::v1::DaemonStop stop_req;
        check_client.stop_daemon(stop_req);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
      }
    } catch (...) {
      // Ignore if no daemon running
    }

#ifdef _WIN32
    std::string exe = ISS_DAEMON_PATH;
    std::string cmdline = "\"" + exe + "\" --log-level warn";

    STARTUPINFOA si{};
    PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);

    HANDLE hNull = CreateFileA("NUL", GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = hNull;
    si.hStdOutput = hNull;
    si.hStdError = hNull;

    std::vector<char> cmd_buf(cmdline.begin(), cmdline.end());
    cmd_buf.push_back('\0');

    DWORD flags = DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP;

    BOOL ok = CreateProcessA(nullptr, cmd_buf.data(), NULL, NULL, FALSE, flags,
                             NULL, NULL, &si, &pi);

    if (!ok) {
      DWORD err = GetLastError();
      CloseHandle(hNull);
      FAIL() << "Child daemon launch failed (error=" << err << ")";
    }

    CloseHandle(hNull);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
#else
    pid_t child_pid = fork();

    if (child_pid < 0) {
      FAIL() << "Child daemon fork failed";
    }

    if (child_pid == 0) {
      setsid();

      int devnull = open("daemon.log", O_RDWR | O_CREAT | O_TRUNC, 0644);
      if (devnull >= 0) {
        dup2(devnull, STDIN_FILENO);
        dup2(devnull, STDOUT_FILENO);
        dup2(devnull, STDERR_FILENO);
        close(devnull);
      }

      std::string daemon_path = ISS_DAEMON_PATH;
      execl(daemon_path.c_str(), daemon_path.c_str(), "--log-level", "warn",
            nullptr);

      _exit(1);
    }
#endif

    bool running = false;
    for (int i = 0; i < 20; ++i) {
      try {
        instserver::client::InstrumentServerClient test_client(get_port());
        instserver::daemon::v1::DaemonStatusRequest req;
        auto resp = test_client.daemon_status(req);
        if (resp.running()) {
          running = true;
          break;
        }
      } catch (...) {
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (!running) {
      FAIL() << "Daemon failed to start";
    }

    client = std::make_unique<instserver::client::InstrumentServerClient>(
        get_port());

    // Register / Start MockInstrument1
    v1::StartInstrumentRequest req;
    std::string config_path =
        (std::filesystem::path(TEST_DATA_DIR) / "mock_instrument1.yaml")
            .generic_string();
    req.set_config_path(config_path);
    std::filesystem::path plugin_path =
        instserver::test::get_test_plugin_path("mock_plugin");
    req.set_plugin_path(plugin_path.generic_string());
    req.set_log_level("warn");

    try {
      client->start_instrument(req);
    } catch (const std::exception &e) {
      instserver::client::v1::DaemonStop daemon_stop_req;
      try {
        client->stop_daemon(daemon_stop_req);
      } catch (...) {
      }
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

      instserver::client::v1::DaemonStop daemon_stop_req;
      try {
        client->stop_daemon(daemon_stop_req);
      } catch (...) {
      }
      client.reset();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
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
      function main(context)
        for i = 1, 10 do
          context:call('MockInstrument1.IDN')
        end
      end
    )";
    ofs.close();
  }

  // Warm up
  run_job_to_completion(*client, script_path.generic_string());

  const int num_calls = 100000;
  {
    std::ofstream ofs(script_path);
    ofs << "function main(context)\n";
    ofs << "  for i = 1, " << num_calls << " do\n";
    ofs << "    context:call('MockInstrument1.IDN')\n";
    ofs << "  end\n";
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
    ofs << "function main(context)\n";
    ofs << "  for i = 1, " << num_calls << " do\n";
    ofs << "    context:call('MockInstrument1.SET', {voltage = 5.0})\n";
    ofs << "  end\n";
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
      req.set_plugin_path(instserver::test::get_test_plugin_path("mock_plugin")
                              .generic_string());
      req.set_log_level("warn");
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
    ofs << "function main(context)\n";
    ofs << "  for i = 1, " << calls_per_instrument << " do\n";
    ofs << "    context:call('MockInstrument1.IDN')\n";
    for (const auto &name : instrument_names) {
      ofs << "    context:call('" << name << ".IDN')\n";
    }
    ofs << "  end\n";
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
  req.set_plugin_path(
      instserver::test::get_test_plugin_path("mock_plugin").generic_string());
  req.set_log_level("warn");
  client->start_instrument(req);

  const int num_parallel_blocks = 10000;
  std::filesystem::path script_path = temp_dir / "parallel_exec.lua";
  {
    std::ofstream ofs(script_path);
    ofs << "function main(context)\n";
    ofs << "  for i = 1, " << num_parallel_blocks << " do\n";
    ofs << R"(
      context:parallel(function()
        context:call('MockInstrument1.IDN')
        context:call('MockInstrument2.IDN')
      end)
    )";
    ofs << "  end\n";
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
