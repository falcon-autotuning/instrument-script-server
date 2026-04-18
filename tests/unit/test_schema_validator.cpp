#include <cstdlib>
#include <filesystem>
#include <gtest/gtest.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

/**
 * @brief Debug helper to log paths and commands
 */
void debug_log(const std::string &prefix, const std::string &message) {
  std::cerr << "[DEBUG] " << prefix << ": " << message << std::endl;
}

/**
 * @brief Get the directory containing the executables (one level up from test
 * dir)
 */
fs::path get_executables_dir() {
  fs::path exe_dir = fs::current_path().parent_path();
  debug_log("get_executables_dir",
            "Executables directory: " + exe_dir.string());
  return exe_dir;
}

/**
 * @brief Find an executable in the build/release directory
 */
fs::path find_executable(const std::string &exe_name) {
  fs::path exe_dir = get_executables_dir();
  fs::path exe_path = exe_dir / exe_name;

#ifdef _WIN32
  if (!exe_path.has_extension()) {
    exe_path += ".exe";
  }
#endif

  debug_log("find_executable", "Looking for: " + exe_path.string());
  debug_log("find_executable",
            "Exists: " + std::string(fs::exists(exe_path) ? "YES" : "NO"));

  if (!fs::exists(exe_path)) {
    throw std::runtime_error("Could not find executable: " + exe_path.string());
  }

  debug_log("find_executable", "Found executable: " + exe_path.string());
  return exe_path;
}

/**
 * @brief Get the repository root directory
 */
fs::path get_repo_root() {
  fs::path test_file = fs::absolute(__FILE__);
  debug_log("get_repo_root", "Test file: " + test_file.string());

  fs::path repo_root = test_file.parent_path().parent_path().parent_path();
  debug_log("get_repo_root", "Calculated repo root: " + repo_root.string());
  debug_log("get_repo_root",
            "Repo root exists: " +
                std::string(fs::exists(repo_root) ? "YES" : "NO"));

  return repo_root;
}

/**
 * @brief Get the build output directory for temporary test files
 */
fs::path get_build_output_dir() {
  fs::path build_dir = get_executables_dir();
  debug_log("get_build_output_dir",
            "Build output directory: " + build_dir.string());
  return build_dir;
}

/**
 * @brief Execute a command with arguments using native platform APIs
 * @return Exit code of the executed process
 */
int execute_process(const fs::path &exe_path,
                    const std::vector<std::string> &args) {
  debug_log("execute_process", "Executable: " + exe_path.string());

  for (size_t i = 0; i < args.size(); ++i) {
    debug_log("execute_process",
              "  Arg[" + std::to_string(i) + "]: " + args[i]);
  }

#ifdef _WIN32
  // Windows: Use CreateProcessA
  std::string exe_str = exe_path.string();

  // Build command line: "exe" "arg1" "arg2" ...
  std::string cmd_line = "\"" + exe_str + "\"";
  for (const auto &arg : args) {
    cmd_line += " \"" + arg + "\"";
  }

  debug_log("execute_process", "Full command line: " + cmd_line);

  STARTUPINFOA si = {};
  PROCESS_INFORMATION pi = {};
  si.cb = sizeof(si);

  BOOL success =
      CreateProcessA(NULL,                                 // lpApplicationName
                     const_cast<char *>(cmd_line.c_str()), // lpCommandLine
                     NULL,  // lpProcessAttributes
                     NULL,  // lpThreadAttributes
                     FALSE, // bInheritHandles
                     0,     // dwCreationFlags
                     NULL,  // lpEnvironment
                     NULL,  // lpCurrentDirectory
                     &si,   // lpStartupInfo
                     &pi    // lpProcessInformation
      );

  if (!success) {
    DWORD error = GetLastError();
    throw std::runtime_error("CreateProcessA failed with error code: " +
                             std::to_string(error));
  }

  // Wait for process to complete
  WaitForSingleObject(pi.hProcess, INFINITE);

  DWORD exit_code = 0;
  GetExitCodeProcess(pi.hProcess, &exit_code);

  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);

  debug_log("execute_process",
            "Process exit code: " + std::to_string(exit_code));
  return exit_code;

#else
  // Linux/Mac: Use fork/execvp
  pid_t pid = fork();

  if (pid == -1) {
    throw std::runtime_error("fork() failed");
  }

  if (pid == 0) {
    // Child process
    std::vector<char *> argv;
    argv.push_back(const_cast<char *>(exe_path.filename().string().c_str()));
    for (const auto &arg : args) {
      argv.push_back(const_cast<char *>(arg.c_str()));
    }
    argv.push_back(nullptr);

    execvp(exe_path.string().c_str(), argv.data());
    // If execvp returns, there was an error
    std::cerr << "execvp failed for: " << exe_path.string() << std::endl;
    exit(1);
  } else {
    // Parent process: wait for child
    int status;
    waitpid(pid, &status, 0);

    if (WIFEXITED(status)) {
      int exit_code = WEXITSTATUS(status);
      debug_log("execute_process",
                "Process exit code: " + std::to_string(exit_code));
      return exit_code;
    } else {
      throw std::runtime_error("Child process did not exit normally");
    }
  }
#endif
}

/**
 * @brief Helper to expand template using the template_expander tool
 */
std::string expand_template(const std::string &tmpl_path) {
  debug_log("expand_template", "Input template path: " + tmpl_path);

  fs::path expander_exe = find_executable("template-expander");
  fs::path output_dir = get_build_output_dir();
  fs::path expanded_path = output_dir / "test_expanded.yaml";

  debug_log("expand_template",
            "Template expander exe: " + expander_exe.string());
  debug_log("expand_template", "Output path: " + expanded_path.string());

  std::vector<std::string> args = {tmpl_path, expanded_path.string()};

  int ret = execute_process(expander_exe, args);

  if (ret != 0) {
    debug_log("expand_template", "FAILED - Return code was non-zero");
    throw std::runtime_error("Failed to expand template: " + tmpl_path +
                             " (return code: " + std::to_string(ret) + ")");
  }

  debug_log("expand_template",
            "SUCCESS - Template expanded to: " + expanded_path.string());
  return expanded_path.string();
}

/**
 * @brief Helper to generate configuration using the generate-instrument-config
 * tool
 */
std::string generate_configuration(const std::string &tmpl_path) {
  debug_log("generate_configuration", "Input template path: " + tmpl_path);

  fs::path generator_exe = find_executable("generate-instrument-config");
  fs::path output_dir = get_build_output_dir();
  fs::path config_path = output_dir / "test_config.yaml";

  debug_log("generate_configuration",
            "Generator exe: " + generator_exe.string());
  debug_log("generate_configuration", "Output path: " + config_path.string());

  std::vector<std::string> args = {tmpl_path, config_path.string()};

  int ret = execute_process(generator_exe, args);

  if (ret != 0) {
    debug_log("generate_configuration", "FAILED - Return code was non-zero");
    throw std::runtime_error(
        "Failed to generate configuration: " + config_path.string() +
        " (return code: " + std::to_string(ret) + ")");
  }

  debug_log("generate_configuration", "SUCCESS - Configuration generated");
  return config_path.string();
}

/**
 * @brief Helper to run a CLI validator tool
 */
int run_validator(const std::string &tool, const std::string &yaml_path) {
  debug_log("run_validator", "Tool: " + tool);
  debug_log("run_validator", "YAML path: " + yaml_path);
  debug_log("run_validator",
            "File exists: " +
                std::string(fs::exists(yaml_path) ? "YES" : "NO"));

  fs::path validator_exe = find_executable(tool);
  std::vector<std::string> args = {yaml_path};

  return execute_process(validator_exe, args);
}

/**
 * @brief Clean up temporary test files
 */
void cleanup_temp_files() {
  fs::path output_dir = get_build_output_dir();

  try {
    fs::path expanded_file = output_dir / "test_expanded.yaml";
    if (fs::exists(expanded_file)) {
      debug_log("cleanup", "Removing: " + expanded_file.string());
      fs::remove(expanded_file);
    }
  } catch (const std::exception &e) {
    debug_log("cleanup",
              "Failed to remove test_expanded.yaml: " + std::string(e.what()));
  }

  try {
    fs::path config_file = output_dir / "test_config.yaml";
    if (fs::exists(config_file)) {
      debug_log("cleanup", "Removing: " + config_file.string());
      fs::remove(config_file);
    }
  } catch (const std::exception &e) {
    debug_log("cleanup",
              "Failed to remove test_config.yaml: " + std::string(e.what()));
  }
}

TEST(SchemaValidatorTest, ValidateAgilentInstrumentDirect) {
  std::cerr << "\n======== TEST: ValidateAgilentInstrumentDirect ========\n";

  fs::path repo_root = get_repo_root();
  fs::path yaml_path =
      repo_root / "examples" / "instrument-apis" / "agi_34401a.yaml";

  debug_log("TEST", "Full YAML path: " + yaml_path.string());
  debug_log("TEST", "File exists: " +
                        std::string(fs::exists(yaml_path) ? "YES" : "NO"));

  if (!fs::exists(yaml_path)) {
    debug_log("TEST", "FAIL - File not found!");
    FAIL() << "Test file not found: " << yaml_path.string();
  }

  int ret = run_validator("validate-instrument-api", yaml_path.string());
  EXPECT_EQ(ret, 0) << "Validation failed for Agilent instrument API";
  std::cerr << "========\n\n";
}

TEST(SchemaValidatorTest, ValidateAgilentInstrumentWithExpander) {
  std::cerr
      << "\n======== TEST: ValidateAgilentInstrumentWithExpander ========\n";

  fs::path repo_root = get_repo_root();
  fs::path yaml_path =
      repo_root / "examples" / "instrument-apis" / "agi_34401a.yaml";

  debug_log("TEST", "Full YAML path: " + yaml_path.string());
  debug_log("TEST", "File exists: " +
                        std::string(fs::exists(yaml_path) ? "YES" : "NO"));

  if (!fs::exists(yaml_path)) {
    debug_log("TEST", "FAIL - File not found!");
    FAIL() << "Test file not found: " << yaml_path.string();
  }

  try {
    std::string expanded_path = expand_template(yaml_path.string());
    int ret = run_validator("validate-instrument-api", expanded_path);
    EXPECT_EQ(ret, 0)
        << "Validation failed for expanded Agilent instrument API";
  } catch (const std::exception &e) {
    FAIL() << "Exception: " << e.what();
  }

  cleanup_temp_files();
  std::cerr << "========\n\n";
}

TEST(SchemaValidatorTest, ValidateKeysightInstrument) {
  std::cerr << "\n======== TEST: ValidateKeysightInstrument ========\n";

  fs::path repo_root = get_repo_root();
  fs::path yaml_path =
      repo_root / "examples" / "instrument-apis" / "dso9254a.yaml.tmpl";

  debug_log("TEST", "Full YAML path: " + yaml_path.string());
  debug_log("TEST", "File exists: " +
                        std::string(fs::exists(yaml_path) ? "YES" : "NO"));

  if (!fs::exists(yaml_path)) {
    debug_log("TEST", "FAIL - File not found!");
    FAIL() << "Test file not found: " << yaml_path.string();
  }

  try {
    std::string expanded_path = expand_template(yaml_path.string());
    int ret = run_validator("validate-instrument-api", expanded_path);
    EXPECT_EQ(ret, 0)
        << "Validation failed for expanded Keysight instrument API";
  } catch (const std::exception &e) {
    FAIL() << "Exception: " << e.what();
  }

  cleanup_temp_files();
  std::cerr << "========\n\n";
}

TEST(SchemaValidatorTest, ValidateQuantumDotDeviceConfig) {
  std::cerr << "\n======== TEST: ValidateQuantumDotDeviceConfig ========\n";

  fs::path repo_root = get_repo_root();
  fs::path yaml_path =
      repo_root / "examples" / "one_charge_sensor_quantum_dot_device.yaml";

  debug_log("TEST", "Full YAML path: " + yaml_path.string());
  debug_log("TEST", "File exists: " +
                        std::string(fs::exists(yaml_path) ? "YES" : "NO"));

  if (!fs::exists(yaml_path)) {
    debug_log("TEST", "FAIL - File not found!");
    FAIL() << "Test file not found: " << yaml_path.string();
  }

  int ret = run_validator("validate-quantum-dot-config", yaml_path.string());
  EXPECT_EQ(ret, 0) << "Validation failed for quantum dot device config";
  std::cerr << "========\n\n";
}

TEST(SchemaValidatorTest, GenerateAndValidateAgilentInstrumentConfiguration) {
  std::cerr << "\n======== TEST: "
               "GenerateAndValidateAgilentInstrumentConfiguration ========\n";

  fs::path repo_root = get_repo_root();
  fs::path yaml_path =
      repo_root / "examples" / "instrument-apis" / "agi_34401a.yaml";

  debug_log("TEST", "Full YAML path: " + yaml_path.string());
  debug_log("TEST", "File exists: " +
                        std::string(fs::exists(yaml_path) ? "YES" : "NO"));

  if (!fs::exists(yaml_path)) {
    debug_log("TEST", "FAIL - File not found!");
    FAIL() << "Test file not found: " << yaml_path.string();
  }

  try {
    std::string api_path = expand_template(yaml_path.string());
    auto config_path = generate_configuration(api_path);
    auto ret2 = run_validator("validate-instrument-config", config_path);
    EXPECT_EQ(ret2, 0)
        << "Validation failed for generated Agilent instrument configuration";
  } catch (const std::exception &e) {
    FAIL() << "Exception: " << e.what();
  }

  cleanup_temp_files();
  std::cerr << "========\n\n";
}

TEST(SchemaValidatorTest, GenerateAndValidateKeysightInstrumentConfiguration) {
  std::cerr << "\n======== TEST: "
               "GenerateAndValidateKeysightInstrumentConfiguration ========\n";

  fs::path repo_root = get_repo_root();
  fs::path yaml_path =
      repo_root / "examples" / "instrument-apis" / "dso9254a.yaml.tmpl";

  debug_log("TEST", "Full YAML path: " + yaml_path.string());
  debug_log("TEST", "File exists: " +
                        std::string(fs::exists(yaml_path) ? "YES" : "NO"));

  if (!fs::exists(yaml_path)) {
    debug_log("TEST", "FAIL - File not found!");
    FAIL() << "Test file not found: " << yaml_path.string();
  }

  try {
    std::string api_path = expand_template(yaml_path.string());
    auto config_path = generate_configuration(api_path);
    auto ret2 = run_validator("validate-instrument-config", config_path);
    EXPECT_EQ(ret2, 0)
        << "Validation failed for generated Keysight instrument configuration";
  } catch (const std::exception &e) {
    FAIL() << "Exception: " << e.what();
  }

  cleanup_temp_files();
  std::cerr << "========\n\n";
}
