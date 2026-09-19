#include "instrument-script-server/core/InstrumentCommand.hpp"
#include "instrument-script-server/ipc/IPCMessage.hpp"

#include <gtest/gtest.h>
#include <instrument-plugin.h>

using namespace instserver;
using namespace instserver::ipc;
void copy_string(char *dst, size_t dst_size, const std::string &src) {
  std::strncpy(dst, src.c_str(), dst_size - 1);
  dst[dst_size - 1] = '\0';
}
static const Variable *find_param(const InstrumentCommand &cmd,
                                  const std::string &name) {
  for (const auto &param : cmd.params) {
    if (param.name == name) {
      return &param;
    }
  }
  return nullptr;
}

TEST(Serialization, CommandBasic) {
  InstrumentCommand cmd;
  cmd.id = "test-123";
  cmd.instrument_name = "DMM1";
  cmd.verb = "MEASURE";
  cmd.expects_response = true;
  cmd.timeout = std::chrono::milliseconds(1000);

  std::vector<ipc::IPCMessage> ipc_cmds;
  ipc::fill_ipc_commands(ipc_cmds, cmd);
  InstrumentCommand deserialized = ipc::from_ipc_commands(ipc_cmds);

  EXPECT_EQ(deserialized.id, "test-123");
  EXPECT_EQ(deserialized.verb, "MEASURE");
  EXPECT_EQ(deserialized.timeout.count(), 1000);
}

TEST(Serialization, CheckVariableReflections) {
  InstrumentCommand cmd;
  cmd.id = "test-456";
  cmd.instrument_name = "DAC1";
  cmd.verb = "SET_VOLTAGE";

  // channel (int64)
  {
    Variable var;
    copy_string(var.name, PLUGIN_MAX_STRING_LEN, "channel");
    var.type = PARAM_TYPE_INT64;
    var.value.i64_val = 1;
    cmd.params.push_back(var);
  }

  // voltage (double)
  {
    Variable var;
    copy_string(var.name, PLUGIN_MAX_STRING_LEN, "voltage");
    var.type = PARAM_TYPE_DOUBLE;
    var.value.d_val = 5.5;
    cmd.params.push_back(var);
  }

  // label (string)
  {
    Variable var;
    copy_string(var.name, PLUGIN_MAX_STRING_LEN, "label");
    var.type = PARAM_TYPE_STRING;
    copy_string(var.value.str_val, PLUGIN_MAX_STRING_LEN, "Gate1");
    cmd.params.push_back(var);
  }

  // data (buffer)
  {
    Variable var;
    copy_string(var.name, PLUGIN_MAX_STRING_LEN, "data");
    var.type = PARAM_TYPE_BUFFER;
    copy_string(var.value.str_val, PLUGIN_MAX_STRING_LEN, "number");
    cmd.params.push_back(var);
  }

  // enabled (bool)
  {
    Variable var;
    copy_string(var.name, PLUGIN_MAX_STRING_LEN, "enabled");
    var.type = PARAM_TYPE_BOOL;
    var.value.b_val = true;
    cmd.params.push_back(var);
  }

  std::vector<ipc::IPCMessage> ipc_cmds;
  ipc::fill_ipc_commands(ipc_cmds, cmd);
  InstrumentCommand deserialized = ipc::from_ipc_commands(ipc_cmds);

  EXPECT_EQ(deserialized.params.size(), 5);

  const Variable *channel = find_param(deserialized, "channel");
  ASSERT_NE(channel, nullptr);
  EXPECT_EQ(channel->type, PARAM_TYPE_INT64);
  EXPECT_EQ(channel->value.i64_val, 1);

  const Variable *voltage = find_param(deserialized, "voltage");
  ASSERT_NE(voltage, nullptr);
  EXPECT_EQ(voltage->type, PARAM_TYPE_DOUBLE);
  EXPECT_DOUBLE_EQ(voltage->value.d_val, 5.5);

  const Variable *label = find_param(deserialized, "label");
  ASSERT_NE(label, nullptr);
  EXPECT_EQ(label->type, PARAM_TYPE_STRING);
  EXPECT_STREQ(label->value.str_val, "Gate1");

  const Variable *data = find_param(deserialized, "data");
  ASSERT_NE(data, nullptr);
  EXPECT_EQ(data->type, PARAM_TYPE_BUFFER);
  EXPECT_STREQ(data->value.str_val, "number");

  const Variable *enabled = find_param(deserialized, "enabled");
  ASSERT_NE(enabled, nullptr);
  EXPECT_EQ(enabled->type, PARAM_TYPE_BOOL);
  EXPECT_TRUE(enabled->value.b_val);
}

TEST(Serialization, CommandWithExplicitChunking) {
  InstrumentCommand cmd;
  cmd.id = "test-456";
  cmd.instrument_name = "DAC1";
  cmd.verb = "SET_VALUES";

  // Generate params: param1 → param20
  for (int i = 1; i <= 20; ++i) {
    Variable var;

    std::string name = "param" + std::to_string(i);
    copy_string(var.name, PLUGIN_MAX_STRING_LEN, name);

    var.type = PARAM_TYPE_INT64;
    var.value.i64_val = i;

    cmd.params.push_back(var);
  }

  std::vector<ipc::IPCMessage> ipc_cmds;
  ipc::fill_ipc_commands(ipc_cmds, cmd);
  EXPECT_GT(ipc_cmds.size(), 1); // ensure chunking actually happened
  InstrumentCommand deserialized = ipc::from_ipc_commands(ipc_cmds);

  EXPECT_EQ(deserialized.params.size(), 20);

  for (int i = 1; i <= 20; ++i) {
    std::string name = "param" + std::to_string(i);

    const Variable *p = find_param(deserialized, name);
    ASSERT_NE(p, nullptr);

    EXPECT_EQ(p->type, PARAM_TYPE_INT64);
    EXPECT_EQ(p->value.i64_val, i);
  }
}

TEST(Serialization, CommandWithSyncToken) {
  InstrumentCommand cmd;
  cmd.id = "sync-cmd";
  cmd.instrument_name = "DAC1";
  cmd.verb = "SET";
  cmd.sync_token = 42;

  std::vector<ipc::IPCMessage> ipc_cmds;
  ipc::fill_ipc_commands(ipc_cmds, cmd);
  InstrumentCommand deserialized = ipc::from_ipc_commands(ipc_cmds);

  ASSERT_TRUE(deserialized.sync_token.has_value());
  EXPECT_EQ(*deserialized.sync_token, 42);
}

TEST(Serialization, ResponseSuccess) {
  InstrumentCommandResponse resp;
  resp.id = "cmd-789";

  {
    VariableWithUnit var;
    copy_string(var.var.name, PLUGIN_MAX_STRING_LEN, "voltage");
    var.var.type = PARAM_TYPE_DOUBLE;
    var.var.value.d_val = 3.15159;
    std::strncpy(var.unit.data(), "V", MAX_UNIT_LEN - 1);
    var.unit[MAX_UNIT_LEN - 1] = '\0';
    resp.returns.push_back(var);
  }

  std::vector<ipc::IPCMessage> ipc_resp;
  ipc::fill_ipc_responses(ipc_resp, resp);
  InstrumentCommandResponse deserialized = ipc::from_ipc_responses(ipc_resp);

  EXPECT_EQ(deserialized.id, "cmd-789");
  ASSERT_TRUE(deserialized.returns.size());
  EXPECT_EQ(deserialized.returns[0].var.type, PARAM_TYPE_DOUBLE);
  EXPECT_DOUBLE_EQ(deserialized.returns[0].var.value.d_val, 3.15159);
  EXPECT_STREQ(deserialized.returns[0].unit.data(), "V");
}

TEST(Serialization, ResponseAllTypesWithChunking) {
  InstrumentCommandResponse resp;
  resp.id = "resp-all-types";

  // ---- generate mixed types across many entries (forces chunking) ----
  for (int i = 1; i <= 20; ++i) {
    VariableWithUnit var;

    std::string name = "var" + std::to_string(i);
    copy_string(var.var.name, PLUGIN_MAX_STRING_LEN, name);

    switch (i % 5) {
    case 0: // double
      var.var.type = PARAM_TYPE_DOUBLE;
      var.var.value.d_val = static_cast<double>(i) * 1.1;
      std::strncpy(var.unit.data(), "V", MAX_UNIT_LEN - 1);
      var.unit[MAX_UNIT_LEN - 1] = '\0';
      break;

    case 1: // int64
      var.var.type = PARAM_TYPE_INT64;
      var.var.value.i64_val = i;
      break;

    case 2: // string
      var.var.type = PARAM_TYPE_STRING;
      copy_string(var.var.value.str_val, PLUGIN_MAX_STRING_LEN,
                  ("str" + std::to_string(i)).c_str());
      break;

    case 3: // buffer
      var.var.type = PARAM_TYPE_BUFFER;
      copy_string(var.var.value.str_val, PLUGIN_MAX_STRING_LEN,
                  ("buf" + std::to_string(i)).c_str());
      std::strncpy(var.unit.data(), "A", MAX_UNIT_LEN - 1);
      var.unit[MAX_UNIT_LEN - 1] = '\0';
      break;

    case 4: // bool
      var.var.type = PARAM_TYPE_BOOL;
      var.var.value.b_val = (i % 2 == 0);
      break;
    }

    resp.returns.push_back(var);
  }

  // ---- serialize ----
  std::vector<ipc::IPCMessage> ipc_resp;
  ipc::fill_ipc_responses(ipc_resp, resp);

  // ✅ ensure actual chunking happened
  EXPECT_GT(ipc_resp.size(), 1);

  // ---- deserialize ----
  InstrumentCommandResponse deserialized = ipc::from_ipc_responses(ipc_resp);

  // ---- verify size ----
  EXPECT_EQ(deserialized.returns.size(), 20);

  // ---- verify all values ----
  for (int i = 1; i <= 20; ++i) {
    std::string name = "var" + std::to_string(i);

    const VariableWithUnit *var = nullptr;
    for (const auto &r : deserialized.returns) {
      if (r.var.name == name) {
        var = &r;
        break;
      }
    }

    ASSERT_NE(var, nullptr);
    const Variable *v = &var->var;

    switch (i % 5) {
    case 0:
      EXPECT_EQ(v->type, PARAM_TYPE_DOUBLE);
      EXPECT_DOUBLE_EQ(v->value.d_val, static_cast<double>(i) * 1.1);
      EXPECT_STREQ(var->unit.data(), "V");
      break;

    case 1:
      EXPECT_EQ(v->type, PARAM_TYPE_INT64);
      EXPECT_EQ(v->value.i64_val, i);
      break;

    case 2:
      EXPECT_EQ(v->type, PARAM_TYPE_STRING);
      EXPECT_EQ(std::string(v->value.str_val), "str" + std::to_string(i));
      break;

    case 3:
      EXPECT_EQ(v->type, PARAM_TYPE_BUFFER);
      EXPECT_EQ(std::string(v->value.str_val), "buf" + std::to_string(i));
      EXPECT_STREQ(var->unit.data(), "A");
      break;

    case 4:
      EXPECT_EQ(v->type, PARAM_TYPE_BOOL);
      EXPECT_EQ(v->value.b_val, (i % 2 == 0));
      break;
    }
  }
}
