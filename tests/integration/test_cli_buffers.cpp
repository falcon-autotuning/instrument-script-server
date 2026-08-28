#include "test_cli_helpers.hpp"

class CLITestBuffers : public ::testing::Test {
protected:
  void SetUp() override {
    run_iss("daemon start --json");
    ASSERT_TRUE(wait_for_daemon_started())
        << "Daemon never became reachable in SetUp";
  }
  void TearDown() override {
    run_iss("inst stop TestScope");
    auto [exit_code, output] = run_iss("daemon stop");
    EXPECT_EQ(exit_code, 0) << "daemon stop failed:\n" << output;
    EXPECT_TRUE(wait_for_daemon_stopped(5000)) << "Daemon never stopped";
  }
};

// --- Migrated Buffer tests from test_cli_commands.cpp ---

// list-buffers with no buffers should succeed and say so.
TEST_F(CLITestBuffers, ListBuffersWhenEmpty) {
  auto [rc, out] = run_iss("buffer list");
  EXPECT_EQ(rc, 0) << "buffer list failed:\n" << out;
  EXPECT_NE(out.find("No active shared memory buffers"), std::string::npos)
      << "Expected 'No active shared memory buffers':\n"
      << out;
}

// read-buffer with a non-existent ID should fail cleanly.
TEST_F(CLITestBuffers, ReadNonExistentBufferFails) {
  auto [rc, out] = run_iss("buffer read this_id_does_not_exist_xyz");
  EXPECT_NE(rc, 0) << "Expected non-zero exit for bad buffer ID:\n" << out;
}

// buffer-metadata with a non-existent ID should fail cleanly.
TEST_F(CLITestBuffers, MetadataForNonExistentBufferFails) {
  auto [rc, out] = run_iss("buffer metadata this_id_does_not_exist_xyz");
  EXPECT_NE(rc, 0) << "Expected non-zero exit for bad buffer ID:\n" << out;
}

// release-buffer with a non-existent ID is idempotent: the server
// returns ok:true as a no-op rather than treating an unknown ID as an error.
TEST_F(CLITestBuffers, ReleaseNonExistentBufferIsIdempotent) {
  auto [rc, out] = run_iss("buffer release this_id_does_not_exist_xyz");
  EXPECT_EQ(rc, 0) << "Expected idempotent (exit 0) for unknown buffer ID:\n"
                   << out;
}

// read-buffer requires an argument.
TEST_F(CLITestBuffers, ReadBufferMissingArgFails) {
  auto [rc, out] = run_iss("buffer read");
  EXPECT_NE(rc, 0) << "Expected non-zero when buffer ID omitted:\n" << out;
}

// --- New Buffer Lifecycle Test ---

TEST_F(CLITestBuffers, BufferLifecycleCreateListReadRelease) {
  // Create TestScope configuration
  std::filesystem::path config_path =
      std::filesystem::temp_directory_path() / "test_scope_cli_large.yaml";
  std::ofstream ofs(config_path);
  ofs << "name: TestScope\n"
      << "api_ref: "
      << (std::filesystem::path(data_dir) / "mock_large_api.yaml").string()
      << "\n"
      << "connection:\n"
      << "  address: \"mock://testscope\"\n"
      << "io_config:\n";
  ofs.close();

  // Start TestScope instrument with the large data visa plugin
  auto [start_rc, start_out] = run_iss("inst start " + config_path.string() +
                                       " --plugin " + mock_large_plugin);
  ASSERT_EQ(start_rc, 0) << "Instrument start failed, output:\n" << start_out;
  std::this_thread::sleep_for(200ms);

  // Run the script that triggers buffer creation
  auto [measure_rc, measure_out] = run_iss(
      "measure " +
      (std::filesystem::path(data_dir) / "test_scripts" / "create_buffers.lua")
          .string() +
      " --json");
  EXPECT_EQ(measure_rc, 0) << "measure failed:\n" << measure_out;

  // List active buffers and extract the first ID
  auto [list_rc, list_out] = run_iss("buffer list");
  EXPECT_EQ(list_rc, 0) << "buffer list failed:\n" << list_out;
  std::string buffer_id = extract_first_buffer_id(list_out);
  ASSERT_FALSE(buffer_id.empty()) << "No active buffer found in output:\n"
                                  << list_out;

  // Query buffer metadata
  auto [meta_rc, meta_out] = run_iss("buffer metadata " + buffer_id);
  EXPECT_EQ(meta_rc, 0) << "buffer metadata failed:\n" << meta_out;
  EXPECT_NE(meta_out.find("ID: " + buffer_id), std::string::npos);
  EXPECT_NE(meta_out.find("Elements: 10000"), std::string::npos);
  EXPECT_NE(meta_out.find("Type: float"), std::string::npos);

  // Read buffer
  auto [read_rc, read_out] = run_iss("buffer read " + buffer_id);
  EXPECT_EQ(read_rc, 0) << "buffer read failed:\n" << read_out;

  // Release buffer
  auto [rel_rc, rel_out] = run_iss("buffer release " + buffer_id);
  EXPECT_EQ(rel_rc, 0) << "buffer release failed:\n" << rel_out;
  EXPECT_NE(rel_out.find("Released buffer: " + buffer_id), std::string::npos);

  // Verify buffer is no longer active
  auto [list_rc2, list_out2] = run_iss("buffer list");
  EXPECT_EQ(list_rc2, 0);
  EXPECT_EQ(list_out2.find(buffer_id), std::string::npos);

  // Stop instrument
  auto [stop_rc, stop_out] = run_iss("inst stop TestScope");
  EXPECT_EQ(stop_rc, 0);

  std::filesystem::remove(config_path);
}

TEST_F(CLITestBuffers, BufferListTest) {
  std::filesystem::path config_path =
      std::filesystem::temp_directory_path() / "test_scope_cli_large.yaml";
  std::ofstream ofs(config_path);
  ofs << "name: TestScope\n"
      << "api_ref: "
      << (std::filesystem::path(data_dir) / "mock_large_api.yaml").string()
      << "\n"
      << "connection:\n"
      << "  address: \"mock://testscope\"\n"
      << "io_config:\n";
  ofs.close();

  auto [start_rc, start_out] = run_iss("inst start " + config_path.string() +
                                       " --plugin " + mock_large_plugin);
  ASSERT_EQ(start_rc, 0);
  std::this_thread::sleep_for(200ms);

  run_iss("measure " + (std::filesystem::path(data_dir) / "test_scripts" /
                        "create_buffers.lua")
                           .string());

  auto [list_rc, list_out] = run_iss("buffer list");
  EXPECT_EQ(list_rc, 0);
  std::string buffer_id = extract_first_buffer_id(list_out);
  ASSERT_FALSE(buffer_id.empty()) << "Buffer list output: " << list_out;

  run_iss("buffer release " + buffer_id);
  run_iss("inst stop TestScope");
  std::filesystem::remove(config_path);
}

TEST_F(CLITestBuffers, BufferMetadataTest) {
  std::filesystem::path config_path =
      std::filesystem::temp_directory_path() / "test_scope_cli_large.yaml";
  std::ofstream ofs(config_path);
  ofs << "name: TestScope\n"
      << "api_ref: "
      << (std::filesystem::path(data_dir) / "mock_large_api.yaml").string()
      << "\n"
      << "connection:\n"
      << "  address: \"mock://testscope\"\n"
      << "io_config:\n";
  ofs.close();

  auto [start_rc, start_out] = run_iss("inst start " + config_path.string() +
                                       " --plugin " + mock_large_plugin);
  ASSERT_EQ(start_rc, 0);
  std::this_thread::sleep_for(200ms);

  run_iss("measure " + (std::filesystem::path(data_dir) / "test_scripts" /
                        "create_buffers.lua")
                           .string());

  auto [list_rc, list_out] = run_iss("buffer list");
  std::string buffer_id = extract_first_buffer_id(list_out);

  auto [meta_rc, meta_out] = run_iss("buffer metadata " + buffer_id);
  EXPECT_EQ(meta_rc, 0);
  EXPECT_NE(meta_out.find("Elements: 10000"), std::string::npos);
  EXPECT_NE(meta_out.find("Size: 40000 bytes"), std::string::npos);

  run_iss("buffer release " + buffer_id);
  run_iss("inst stop TestScope");
  std::filesystem::remove(config_path);
}

TEST_F(CLITestBuffers, BufferReadTest) {
  std::filesystem::path config_path =
      std::filesystem::temp_directory_path() / "test_scope_cli_large.yaml";
  std::ofstream ofs(config_path);
  ofs << "name: TestScope\n"
      << "api_ref: "
      << (std::filesystem::path(data_dir) / "mock_large_api.yaml").string()
      << "\n"
      << "connection:\n"
      << "  address: \"mock://testscope\"\n"
      << "io_config:\n";
  ofs.close();

  auto [start_rc, start_out] = run_iss("inst start " + config_path.string() +
                                       " --plugin " + mock_large_plugin);
  ASSERT_EQ(start_rc, 0);
  std::this_thread::sleep_for(200ms);

  run_iss("measure " + (std::filesystem::path(data_dir) / "test_scripts" /
                        "create_buffers.lua")
                           .string());

  auto [list_rc, list_out] = run_iss("buffer list");
  std::string buffer_id = extract_first_buffer_id(list_out);

  auto [read_rc, read_out] = run_iss("buffer read " + buffer_id);
  EXPECT_EQ(read_rc, 0);

  run_iss("buffer release " + buffer_id);
  run_iss("inst stop TestScope");
  std::filesystem::remove(config_path);
}
