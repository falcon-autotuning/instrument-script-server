#include "instrument-server/ipc/WorkerProtocol.hpp"
#include <gtest/gtest.h>

using namespace instserver;
using namespace instserver::ipc;

TEST(WorkerProtocolTest, ParamValueToJsonAllTypes) {
  // Monostate
  EXPECT_TRUE(param_value_to_json(std::monostate{})["value"].is_null());

  // Numeric types
  EXPECT_EQ(param_value_to_json(static_cast<int32_t>(42))["type"], "int32_t");
  EXPECT_EQ(param_value_to_json(static_cast<int64_t>(100))["type"], "int64_t");
  EXPECT_EQ(param_value_to_json(static_cast<uint32_t>(50))["type"], "uint32_t");
  EXPECT_EQ(param_value_to_json(static_cast<uint64_t>(200))["type"],
            "uint64_t");
  EXPECT_EQ(param_value_to_json(3.14f)["type"], "float");
  EXPECT_EQ(param_value_to_json(3.14159)["type"], "double");

  // Boolean
  EXPECT_TRUE(param_value_to_json(true)["value"].get<bool>());
  EXPECT_FALSE(param_value_to_json(false)["value"].get<bool>());

  // String
  EXPECT_EQ(param_value_to_json(std::string("hello"))["value"], "hello");

  // Arrays
  std::vector<double> darr = {1.1, 2.2, 3.3};
  auto jarr = param_value_to_json(darr)["value"];
  EXPECT_TRUE(jarr.is_array());
  EXPECT_EQ(jarr.size(), 3);
  EXPECT_DOUBLE_EQ(jarr[0], 1.1);

  std::vector<int32_t> iarr = {1, 2, 3};
  auto jiarr = param_value_to_json(iarr)["value"];
  EXPECT_TRUE(jiarr.is_array());
  EXPECT_EQ(jiarr.size(), 3);
  EXPECT_EQ(jiarr[0], 1);
}

TEST(WorkerProtocolTest, JsonToParamValueAllTypes) {
  // Null / monostate
  auto null_val = json_to_param_value(
      nlohmann::json{{"type", "monostate"}, {"value", nullptr}});
  EXPECT_TRUE(std::holds_alternative<std::monostate>(null_val));

  // Boolean
  auto bool_val =
      json_to_param_value(nlohmann::json{{"type", "bool"}, {"value", true}});
  ASSERT_TRUE(std::holds_alternative<bool>(bool_val));
  EXPECT_TRUE(std::get<bool>(bool_val));

  // Integer
  auto int_val =
      json_to_param_value(nlohmann::json{{"type", "int64_t"}, {"value", 42}});
  ASSERT_TRUE(std::holds_alternative<int64_t>(int_val));
  EXPECT_EQ(std::get<int64_t>(int_val), 42);

  // Unsigned
  auto uint_val = json_to_param_value(nlohmann::json{
      {"type", "uint64_t"}, {"value", static_cast<uint64_t>(100)}});
  ASSERT_TRUE(std::holds_alternative<uint64_t>(uint_val));
  EXPECT_EQ(std::get<uint64_t>(uint_val), 100);

  // Float (double)
  auto float_val =
      json_to_param_value(nlohmann::json{{"type", "double"}, {"value", 3.14}});
  ASSERT_TRUE(std::holds_alternative<double>(float_val));
  EXPECT_DOUBLE_EQ(std::get<double>(float_val), 3.14);

  // String
  auto str_val = json_to_param_value(
      nlohmann::json{{"type", "string"}, {"value", "hello"}});
  ASSERT_TRUE(std::holds_alternative<std::string>(str_val));
  EXPECT_EQ(std::get<std::string>(str_val), "hello");

  // Array of doubles
  nlohmann::json darr_json = {1.1, 2.2, 3.3};
  auto darr_val = json_to_param_value(
      nlohmann::json{{"type", "vector<double>"}, {"value", darr_json}});
  ASSERT_TRUE(std::holds_alternative<std::vector<double>>(darr_val));
  auto darr = std::get<std::vector<double>>(darr_val);
  EXPECT_EQ(darr.size(), 3);
  EXPECT_DOUBLE_EQ(darr[0], 1.1);

  // Array of ints
  nlohmann::json iarr_json = {1, 2, 3};
  auto iarr_val = json_to_param_value(
      nlohmann::json{{"type", "vector<int32_t>"}, {"value", iarr_json}});
  ASSERT_TRUE(std::holds_alternative<std::vector<int32_t>>(iarr_val));
  auto iarr = std::get<std::vector<int32_t>>(iarr_val);
  EXPECT_EQ(iarr.size(), 3);
  EXPECT_EQ(iarr[0], 1);
}

TEST(WorkerProtocolTest, RoundTripConversion) {
  std::vector<ParamValue> test_values = {std::monostate{},
                                         true,
                                         static_cast<int64_t>(42),
                                         3.14159,
                                         std::string("test"),
                                         std::vector<double>{1.1, 2.2, 3.3},
                                         std::vector<int32_t>{1, 2, 3}};

  for (const auto &val : test_values) {
    auto json = param_value_to_json(val);
    auto back = json_to_param_value(json);

    // Type should match
    EXPECT_EQ(val.index(), back.index());
  }
}

TEST(WorkerProtocolTest, SerializeCommandFullyPopulated) {
  SerializedCommand cmd;
  cmd.id = "test-123";
  cmd.instrument_name = "DMM1";
  cmd.verb = "MEASURE";
  cmd.timeout = std::chrono::milliseconds(5000);
  cmd.priority = 10;
  cmd.expects_response = true;
  cmd.return_type = "float";
  cmd.channel_group = "analog";
  cmd.channel_number = 1;
  cmd.params["voltage"] = 3.3;
  cmd.params["samples"] = static_cast<int64_t>(1000);

  std::string serialized = serialize_command(cmd);

  // Should be valid JSON
  auto j = nlohmann::json::parse(serialized);

  // Should contain all fields
  EXPECT_TRUE(j.contains("id"));
  EXPECT_TRUE(j.contains("instrument_name"));
  EXPECT_TRUE(j.contains("verb"));
  EXPECT_TRUE(j.contains("timeout_ms"));
  EXPECT_TRUE(j.contains("priority"));
  EXPECT_TRUE(j.contains("expects_response"));
  EXPECT_TRUE(j.contains("return_type"));
  EXPECT_TRUE(j.contains("channel_group"));
  EXPECT_TRUE(j.contains("channel_number"));
  EXPECT_TRUE(j.contains("params"));
}

TEST(WorkerProtocolTest, SerializeResponseSuccess) {
  CommandResponse resp;
  resp.command_id = "test-456";
  resp.instrument_name = "SCOPE1";
  resp.success = true;
  resp.text_response = "3.142";
  resp.return_value = 3.142;
  resp.error_code = 0;
  resp.error_message = "";

  std::string serialized = serialize_response(resp);
  auto j = nlohmann::json::parse(serialized);

  EXPECT_EQ(j["command_id"], "test-456");
  EXPECT_EQ(j["instrument_name"], "SCOPE1");
  EXPECT_TRUE(j["success"]);
  EXPECT_EQ(j["text_response"], "3.142");
  EXPECT_NEAR(j["return_value"]["value"].get<double>(), 3.142, 0.001);
}

TEST(WorkerProtocolTest, SerializeResponseFailure) {
  CommandResponse resp;
  resp.command_id = "test-789";
  resp.instrument_name = "DMM1";
  resp.success = false;
  resp.error_code = -100;
  resp.error_message = "Device timeout";

  std::string serialized = serialize_response(resp);
  auto j = nlohmann::json::parse(serialized);

  EXPECT_FALSE(j["success"]);
  EXPECT_EQ(j["error_code"], -100);
  EXPECT_EQ(j["error_message"], "Device timeout");
}
