#include "instrument-script-server/core/ParsingTools.hpp"

#include <filesystem>
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
  type: MockMultimeter
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
