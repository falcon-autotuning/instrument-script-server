#include "instrument-script-server/core/ParsingTools.hpp"

#include <filesystem>
#include <format>
#include <fstream>
#include <gtest/gtest.h>
#include <instrument-plugin.h>
#include <stdexcept>

TEST(ParsingToolsTest, MapsLegacyAndRuntimeTypeNames) {
  EXPECT_EQ(instserver::mapType("float"), PARAM_TYPE_DOUBLE);
  EXPECT_EQ(instserver::mapType("integer"), PARAM_TYPE_INT64);
  EXPECT_EQ(instserver::mapType("int"), PARAM_TYPE_INT64);
  EXPECT_EQ(instserver::mapType("string"), PARAM_TYPE_STRING);
  EXPECT_EQ(instserver::mapType("boolean"), PARAM_TYPE_BOOL);
  EXPECT_EQ(instserver::mapType("bool"), PARAM_TYPE_BOOL);
  EXPECT_EQ(instserver::mapType("array"), PARAM_TYPE_BUFFER);
  EXPECT_EQ(instserver::mapType("buffer"), PARAM_TYPE_BUFFER);
}

TEST(ParsingToolsTest, RejectsUnknownTypeName) {
  EXPECT_THROW(instserver::mapType("unknown"), std::runtime_error);
}

TEST(ParsingToolsTest, LoadsChannelGroupLocalIOReferences) {
  const auto api_path =
      std::filesystem::temp_directory_path() / "iss_channel_group_api.yaml";

  std::ofstream api(api_path);
  api << R"yaml(
api_version: 1.0.0
instrument:
  vendor: Mock
  model: 1
  identifier: Meter1
protocol:
  type: Custom
  name: MockMultimeter
channel_groups:
  - name: analog
    channel_parameter:
      type: int
    io_types:
      - name: sample_rate
        type: int
      - suffix: waveform
        type: array
io:
  - name: global_mode
    type: string
commands:
  SET_SAMPLE_RATE:
    template: SOUR:SAMPle:{analog}:{sample_rate}
    channel_group: analog
    parameters:
      - io: sample_rate
    outputs: []
  GET_WAVEFORM:
    template: MEAS:WAV:{analog}
    channel_group: analog
    parameters: []
    outputs: [waveform]
)yaml";
  api.close();

  const auto commands = instserver::load_api(api_path);

  const auto set_it = commands.find("SET_SAMPLE_RATE");
  ASSERT_NE(set_it, commands.end());
  ASSERT_EQ(set_it->second.parameters.size(), 2U);
  EXPECT_EQ(set_it->second.parameters[0].name, "analog");
  EXPECT_EQ(set_it->second.parameters[0].type, PARAM_TYPE_INT64);
  EXPECT_EQ(set_it->second.parameters[1].name, "sample_rate");
  EXPECT_EQ(set_it->second.parameters[1].type, PARAM_TYPE_INT64);

  const auto get_it = commands.find("GET_WAVEFORM");
  ASSERT_NE(get_it, commands.end());
  ASSERT_EQ(get_it->second.returns.size(), 1U);
  EXPECT_EQ(get_it->second.returns[0].name, "waveform");
  EXPECT_EQ(get_it->second.returns[0].type, PARAM_TYPE_BUFFER);

  std::filesystem::remove(api_path);
}

TEST(ParsingToolsTest, LoadsConfigWithOptionalParams) {
  const auto config_path =
      std::filesystem::temp_directory_path() / "iss_config_test.yaml";
  const auto isa_path =
      std::filesystem::temp_directory_path() / "iss_config_test_api.yaml";
  const std::string addr = "VISA1::ADDR12";
  const std::string type = "VISA";
  const int32_t baudrate = 9600;
  const std::string json = "{\"special\":5}";
  const int32_t delay = 5;
  const std::string init1 = "RST";
  const std::string init2 = "CLR";
  const std::string name = "test_instrument";

  std::string formatted_yaml =
      std::format(R"yaml(
name: {}
api_ref: ./iss_config_test_api.yaml
connection:
  address: {}
  baudrate: {}
  custom: '{}'
startup:
  delay_ms: {}
  init_commands:
    - {}
    - {}
)yaml",
                  name, addr, baudrate, json, delay, init1, init2);
  std::cout << "\n--- DEBUG: Generated YAML ---\n"
            << formatted_yaml << "\n-----------------------------\n";
  std::ofstream config(config_path);
  config << formatted_yaml;
  config.close();
  std::string formatted_api = std::format(R"yaml(
protocol:
  type: {}
)yaml",
                                          type);
  std::cout << "\n--- DEBUG: Generated api YAML ---\n"
            << formatted_api << "\n-----------------------------\n";
  std::ofstream api(isa_path);
  api << formatted_api;
  api.close();

  const auto inst_config = instserver::load_config(config_path);
  ASSERT_EQ(inst_config.address, addr);
  ASSERT_EQ(inst_config.baudrate, baudrate);
  ASSERT_EQ(inst_config.custom, json);
  ASSERT_EQ(inst_config.startup_delay, delay);
  ASSERT_EQ(inst_config.name, name);
  ASSERT_EQ(inst_config.init_commands.value()[0], init1);
  ASSERT_EQ(inst_config.init_commands.value()[1], init2);
  std::filesystem::remove(config_path);
  std::filesystem::remove(isa_path);
}

TEST(ParsingToolsTest, LoadsConfigWithMinimalParams) {
  const auto config_path =
      std::filesystem::temp_directory_path() / "iss_config_test.yaml";
  const auto isa_path =
      std::filesystem::temp_directory_path() / "iss_config_test_api.yaml";
  const std::string type = "Custom";
  const std::string name = "test_instrument";
  const std::string special = "Hello";

  std::string formatted_yaml = std::format(R"yaml(
name: {}
api_ref: ./iss_config_test_api.yaml
)yaml",
                                           name);
  std::cout << "\n--- DEBUG: Generated YAML ---\n"
            << formatted_yaml << "\n-----------------------------\n";
  std::ofstream config(config_path);
  config << formatted_yaml;
  config.close();
  std::string formatted_api = std::format(R"yaml(
protocol:
  type: {}
  name: {}
)yaml",
                                          type, special);
  std::cout << "\n--- DEBUG: Generated api YAML ---\n"
            << formatted_api << "\n-----------------------------\n";
  std::ofstream api(isa_path);
  api << formatted_api;
  api.close();

  const auto inst_config = instserver::load_config(config_path);
  ASSERT_EQ(inst_config.name, name);
  std::filesystem::remove(config_path);
  std::filesystem::remove(isa_path);
}
