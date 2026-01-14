#include "instrument-server/SerializedCommand.hpp"

#include <gtest/gtest.h>

using namespace instserver;
using namespace instserver::ipc;

TEST(Serialization, CommandBasic) {
  SerializedCommand cmd;
  cmd.id = "test-123";
  cmd.instrument_name = "DMM1";
  cmd.verb = "MEASURE";
  cmd.expects_response = true;
  cmd.timeout = std::chrono::milliseconds(1000);

  std::string json = serialize_command(cmd);
  SerializedCommand deserialized = deserialize_command(json);

  EXPECT_EQ(deserialized.id, "test-123");
  EXPECT_EQ(deserialized.instrument_name, "DMM1");
  EXPECT_EQ(deserialized.verb, "MEASURE");
  EXPECT_TRUE(deserialized.expects_response);
  EXPECT_EQ(deserialized.timeout.count(), 1000);
}

TEST(Serialization, CommandWithParams) {
  SerializedCommand cmd;
  cmd.id = "test-456";
  cmd.instrument_name = "DAC1";
  cmd.verb = "SET_VOLTAGE";
  cmd.params["channel"] = static_cast<int64_t>(1);
  cmd.params["voltage"] = 5.5;
  cmd.params["label"] = std::string("Gate1");
  cmd.params["enabled"] = true;

  std::string json = serialize_command(cmd);
  SerializedCommand deserialized = deserialize_command(json);

  EXPECT_EQ(deserialized.params.size(), 4);
  EXPECT_EQ(std::get<int64_t>(deserialized.params["channel"]), 1);
  EXPECT_DOUBLE_EQ(std::get<double>(deserialized.params["voltage"]), 5.5);
  EXPECT_EQ(std::get<std::string>(deserialized.params["label"]), "Gate1");
  EXPECT_TRUE(std::get<bool>(deserialized.params["enabled"]));
}

TEST(Serialization, CommandWithSyncToken) {
  SerializedCommand cmd;
  cmd.id = "sync-cmd";
  cmd.instrument_name = "DAC1";
  cmd.verb = "SET";
  cmd.sync_token = 42;

  std::string json = serialize_command(cmd);
  SerializedCommand deserialized = deserialize_command(json);

  ASSERT_TRUE(deserialized.sync_token.has_value());
  EXPECT_EQ(*deserialized.sync_token, 42);
}

TEST(Serialization, CommandWithArrayParam) {
  SerializedCommand cmd;
  cmd.id = "array-cmd";
  cmd.instrument_name = "Scope1";
  cmd.verb = "SET_WAVEFORM";
  cmd.params["data"] = std::vector<double>{1.0, 2.0, 3.0, 4.0, 5.0};

  std::string json = serialize_command(cmd);
  SerializedCommand deserialized = deserialize_command(json);

  auto data = std::get<std::vector<double>>(deserialized.params["data"]);
  EXPECT_EQ(data.size(), 5);
  EXPECT_DOUBLE_EQ(data[0], 1.0);
  EXPECT_DOUBLE_EQ(data[4], 5.0);
}

TEST(Serialization, ResponseSuccess) {
  CommandResponse resp;
  resp.command_id = "cmd-789";
  resp.instrument_name = "DMM1";
  resp.success = true;
  resp.text_response = "3.14159";
  resp.return_value = 3.14159;

  std::string json = serialize_response(resp);
  CommandResponse deserialized = deserialize_response(json);

  EXPECT_EQ(deserialized.command_id, "cmd-789");
  EXPECT_EQ(deserialized.instrument_name, "DMM1");
  EXPECT_TRUE(deserialized.success);
  EXPECT_EQ(deserialized.text_response, "3.14159");
  ASSERT_TRUE(deserialized.return_value.has_value());
  EXPECT_DOUBLE_EQ(std::get<double>(*deserialized.return_value), 3.14159);
}

TEST(Serialization, ResponseError) {
  CommandResponse resp;
  resp.command_id = "cmd-error";
  resp.instrument_name = "DAC1";
  resp.success = false;
  resp.error_code = -1;
  resp.error_message = "Voltage out of range";

  std::string json = serialize_response(resp);
  CommandResponse deserialized = deserialize_response(json);

  EXPECT_FALSE(deserialized.success);
  EXPECT_EQ(deserialized.error_code, -1);
  EXPECT_EQ(deserialized.error_message, "Voltage out of range");
}

TEST(Serialization, ResponseWithStringReturn) {
  CommandResponse resp;
  resp.command_id = "idn-cmd";
  resp.instrument_name = "DMM1";
  resp.success = true;
  resp.return_value = std::string("Keithley 2400");

  std::string json = serialize_response(resp);
  CommandResponse deserialized = deserialize_response(json);

  ASSERT_TRUE(deserialized.return_value.has_value());
  EXPECT_EQ(std::get<std::string>(*deserialized.return_value), "Keithley 2400");
}

TEST(Serialization, ResponseWithArrayReturn) {
  CommandResponse resp;
  resp.command_id = "sweep-cmd";
  resp.instrument_name = "Scope1";
  resp.success = true;
  resp.return_value = std::vector<double>{0.1, 0.2, 0.3, 0.4};

  std::string json = serialize_response(resp);
  CommandResponse deserialized = deserialize_response(json);

  ASSERT_TRUE(deserialized.return_value.has_value());
  auto data = std::get<std::vector<double>>(*deserialized.return_value);
  EXPECT_EQ(data.size(), 4);
  EXPECT_DOUBLE_EQ(data[2], 0.3);
}
