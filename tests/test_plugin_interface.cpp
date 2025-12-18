#include "instrument-server/plugin/PluginInterface.h"
#include <cstring>
#include <gtest/gtest.h>

// Test plugin interface structures for ABI compatibility

TEST(PluginInterfaceTest, StructSizes) {
  // Ensure struct sizes haven't changed (ABI stability)
  EXPECT_EQ(sizeof(PluginParamType), sizeof(int));
  EXPECT_LE(sizeof(PluginParamValue), 512); // Reasonable upper bound
  EXPECT_LE(sizeof(PluginCommand), 10000);
  EXPECT_LE(sizeof(PluginResponse), 20000);
}

TEST(PluginInterfaceTest, PluginCommandInitialization) {
  PluginCommand cmd = {};

  EXPECT_EQ(cmd.id[0], '\0');
  EXPECT_EQ(cmd.instrument_name[0], '\0');
  EXPECT_EQ(cmd.verb[0], '\0');
  EXPECT_EQ(cmd.param_count, 0);
  EXPECT_EQ(cmd.timeout_ms, 0);
  EXPECT_FALSE(cmd.expects_response);
}

TEST(PluginInterfaceTest, PluginResponseInitialization) {
  PluginResponse resp = {};

  EXPECT_EQ(resp.command_id[0], '\0');
  EXPECT_FALSE(resp.success);
  EXPECT_EQ(resp.error_code, 0);
  EXPECT_EQ(resp.binary_response_size, 0);
}

TEST(PluginInterfaceTest, StringSafety) {
  PluginCommand cmd = {};

  // Test string truncation
  std::string long_id(PLUGIN_MAX_STRING_LEN + 100, 'x');
  strncpy(cmd.id, long_id.c_str(), PLUGIN_MAX_STRING_LEN - 1);
  cmd.id[PLUGIN_MAX_STRING_LEN - 1] = '\0';

  EXPECT_EQ(strlen(cmd.id), PLUGIN_MAX_STRING_LEN - 1);
}

TEST(PluginInterfaceTest, ParamValueTypes) {
  PluginParamValue val = {};

  // Test each type
  val.type = PARAM_TYPE_INT32;
  val.value.i32_val = 42;
  EXPECT_EQ(val.value.i32_val, 42);

  val.type = PARAM_TYPE_DOUBLE;
  val.value.d_val = 3.14;
  EXPECT_DOUBLE_EQ(val.value.d_val, 3.14);

  val.type = PARAM_TYPE_BOOL;
  val.value.b_val = true;
  EXPECT_TRUE(val.value.b_val);

  val.type = PARAM_TYPE_STRING;
  strncpy(val.value.str_val, "test", PLUGIN_MAX_STRING_LEN - 1);
  EXPECT_STREQ(val.value.str_val, "test");
}

TEST(PluginInterfaceTest, MaxParameters) {
  PluginCommand cmd = {};
  cmd.param_count = PLUGIN_MAX_PARAMS;

  for (uint32_t i = 0; i < PLUGIN_MAX_PARAMS; i++) {
    snprintf(cmd.params[i].name, PLUGIN_MAX_STRING_LEN, "param_%u", i);
    cmd.params[i].value.type = PARAM_TYPE_INT32;
    cmd.params[i].value.value.i32_val = i;
  }

  EXPECT_EQ(cmd.param_count, PLUGIN_MAX_PARAMS);
  EXPECT_EQ(cmd.params[PLUGIN_MAX_PARAMS - 1].value.value.i32_val,
            PLUGIN_MAX_PARAMS - 1);
}

TEST(PluginInterfaceTest, MetadataFields) {
  PluginMetadata meta = {};
  meta.api_version = INSTRUMENT_PLUGIN_API_VERSION;
  strncpy(meta.name, "Test Plugin", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(meta.version, "1.0.0", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(meta.protocol_type, "TEST", PLUGIN_MAX_STRING_LEN - 1);

  EXPECT_EQ(meta.api_version, INSTRUMENT_PLUGIN_API_VERSION);
  EXPECT_STREQ(meta.name, "Test Plugin");
  EXPECT_STREQ(meta.version, "1.0.0");
  EXPECT_STREQ(meta.protocol_type, "TEST");
}
