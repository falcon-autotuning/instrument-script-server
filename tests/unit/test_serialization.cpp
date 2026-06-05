#include "instrument-script-server/SerializedCommand.hpp"

#include <gtest/gtest.h>

using namespace instserver;
using namespace instserver::ipc;
static const Param *find_param(const SerializedCommand &cmd,
                               const std::string &name) {
  for (uint8_t i = 0; i < cmd.param_count; ++i) {
    if (cmd.params[i].name == name) {
      return &cmd.params[i];
    }
  }
  return nullptr;
}

TEST(Serialization, CommandBasic) {
  SerializedCommand cmd;
  cmd.id = "test-123";
  cmd.instrument_name = "DMM1";
  cmd.verb = "MEASURE";
  cmd.expects_response = true;
  cmd.timeout = std::chrono::milliseconds(1000);

  ipc::IPCCommand ipc_cmd{};
  ipc::fill_ipc_command(ipc_cmd, cmd);
  SerializedCommand deserialized = ipc::from_ipc_command(ipc_cmd);

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
  cmd.param_count = 0;

  // channel (int64)
  {
    auto &p = cmd.params[cmd.param_count++];
    p.name = "channel";
    p.value.type = ipc::IPCParamValue::Type::INT64;
    p.value.i = 1;
  }

  // voltage (double)
  {
    auto &p = cmd.params[cmd.param_count++];
    p.name = "voltage";
    p.value.type = ipc::IPCParamValue::Type::DOUBLE;
    p.value.d = 5.5;
  }

  // label (string)
  {
    auto &p = cmd.params[cmd.param_count++];
    p.name = "label";
    p.value.type = ipc::IPCParamValue::Type::STRING;
    p.value.str = "Gate1";
  }

  // enabled (bool)
  {
    auto &p = cmd.params[cmd.param_count++];
    p.name = "enabled";
    p.value.type = ipc::IPCParamValue::Type::BOOL;
    p.value.b = true;
  }

  ipc::IPCCommand ipc_cmd{};
  ipc::fill_ipc_command(ipc_cmd, cmd);
  SerializedCommand deserialized = ipc::from_ipc_command(ipc_cmd);

  EXPECT_EQ(deserialized.param_count, 4);

  const Param *channel = find_param(deserialized, "channel");
  ASSERT_NE(channel, nullptr);
  EXPECT_EQ(channel->value.type, ipc::IPCParamValue::Type::INT64);
  EXPECT_EQ(channel->value.i, 1);

  const Param *voltage = find_param(deserialized, "voltage");
  ASSERT_NE(voltage, nullptr);
  EXPECT_EQ(voltage->value.type, ipc::IPCParamValue::Type::DOUBLE);
  EXPECT_DOUBLE_EQ(voltage->value.d, 5.5);

  const Param *label = find_param(deserialized, "label");
  ASSERT_NE(label, nullptr);
  EXPECT_EQ(label->value.type, ipc::IPCParamValue::Type::STRING);
  EXPECT_EQ(label->value.str, "Gate1");

  const Param *enabled = find_param(deserialized, "enabled");
  ASSERT_NE(enabled, nullptr);
  EXPECT_EQ(enabled->value.type, ipc::IPCParamValue::Type::BOOL);
  EXPECT_TRUE(enabled->value.b);
}

TEST(Serialization, CommandWithSyncToken) {
  SerializedCommand cmd;
  cmd.id = "sync-cmd";
  cmd.instrument_name = "DAC1";
  cmd.verb = "SET";
  cmd.sync_token = 42;

  ipc::IPCCommand ipc_cmd{};
  ipc::fill_ipc_command(ipc_cmd, cmd);
  SerializedCommand deserialized = ipc::from_ipc_command(ipc_cmd);

  ASSERT_TRUE(deserialized.sync_token.has_value());
  EXPECT_EQ(*deserialized.sync_token, 42);
}

TEST(Serialization, CommandWithArrayParam) {
  SerializedCommand cmd;
  cmd.id = "array-cmd";
  cmd.instrument_name = "Scope1";
  cmd.verb = "SET_WAVEFORM";

  cmd.param_count = 0;

  {
    auto &p = cmd.params[cmd.param_count++];
    p.name = "data";
    p.value.type = ipc::IPCParamValue::Type::DOUBLE_ARRAY;
    p.value.arr = {1.0, 2.0, 3.0, 4.0, 5.0};
  }

  ipc::IPCCommand ipc_cmd{};
  ipc::fill_ipc_command(ipc_cmd, cmd);
  SerializedCommand deserialized = ipc::from_ipc_command(ipc_cmd);

  const Param *data_param = find_param(deserialized, "data");
  ASSERT_NE(data_param, nullptr);

  EXPECT_EQ(data_param->value.type, ipc::IPCParamValue::Type::DOUBLE_ARRAY);

  const auto &data = data_param->value.arr;
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

  {
    ParamValue v;
    v.type = ipc::IPCParamValue::Type::DOUBLE;
    v.d = 3.14159;
    resp.return_value = std::move(v);
  }

  ipc::IPCResponse ipc_resp{};
  ipc::fill_ipc_response(ipc_resp, resp);
  CommandResponse deserialized = ipc::from_ipc_response(ipc_resp);

  EXPECT_EQ(deserialized.command_id, "cmd-789");
  EXPECT_EQ(deserialized.instrument_name, "DMM1");
  EXPECT_TRUE(deserialized.success);
  EXPECT_EQ(deserialized.text_response, "3.14159");

  ASSERT_TRUE(deserialized.return_value.has_value());
  EXPECT_EQ(deserialized.return_value->type, ipc::IPCParamValue::Type::DOUBLE);
  EXPECT_DOUBLE_EQ(deserialized.return_value->d, 3.14159);
}

TEST(Serialization, ResponseError) {
  CommandResponse resp;
  resp.command_id = "cmd-error";
  resp.instrument_name = "DAC1";
  resp.success = false;
  resp.error_code = -1;
  resp.error_message = "Voltage out of range";

  ipc::IPCResponse ipc_resp{};
  ipc::fill_ipc_response(ipc_resp, resp);
  CommandResponse deserialized = ipc::from_ipc_response(ipc_resp);

  EXPECT_FALSE(deserialized.success);
  EXPECT_EQ(deserialized.error_code, -1);
  EXPECT_EQ(deserialized.error_message, "Voltage out of range");
}

TEST(Serialization, ResponseWithStringReturn) {
  CommandResponse resp;
  resp.command_id = "idn-cmd";
  resp.instrument_name = "DMM1";
  resp.success = true;

  {
    ParamValue v;
    v.type = ipc::IPCParamValue::Type::STRING;
    v.str = "Keithley 2400";
    resp.return_value = std::move(v);
  }

  ipc::IPCResponse ipc_resp{};
  ipc::fill_ipc_response(ipc_resp, resp);
  CommandResponse deserialized = ipc::from_ipc_response(ipc_resp);

  ASSERT_TRUE(deserialized.return_value.has_value());
  EXPECT_EQ(deserialized.return_value->type, ipc::IPCParamValue::Type::STRING);
  EXPECT_EQ(deserialized.return_value->str, "Keithley 2400");
}

TEST(Serialization, ResponseWithArrayReturn) {
  CommandResponse resp;
  resp.command_id = "sweep-cmd";
  resp.instrument_name = "Scope1";
  resp.success = true;

  {
    ParamValue v;
    v.type = ipc::IPCParamValue::Type::DOUBLE_ARRAY;
    v.arr = {0.1, 0.2, 0.3, 0.4};
    resp.return_value = std::move(v);
  }

  ipc::IPCResponse ipc_resp{};
  ipc::fill_ipc_response(ipc_resp, resp);
  CommandResponse deserialized = ipc::from_ipc_response(ipc_resp);

  ASSERT_TRUE(deserialized.return_value.has_value());
  EXPECT_EQ(deserialized.return_value->type,
            ipc::IPCParamValue::Type::DOUBLE_ARRAY);

  const auto &data = deserialized.return_value->arr;
  EXPECT_EQ(data.size(), 4);
  EXPECT_DOUBLE_EQ(data[2], 0.3);
}
