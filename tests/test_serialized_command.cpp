#include "instrument-server/SerializedCommand.hpp"
#include "instrument-server/ipc/WorkerProtocol.hpp"
#include <gtest/gtest.h>

using namespace instserver;

class SerializedCommandTest : public ::testing::Test {
protected:
  void SetUp() override {
    cmd_.id = "test-123";
    cmd_.instrument_name = "DMM1";
    cmd_.verb = "MEASURE_VOLTAGE";
    cmd_.timeout = std::chrono::milliseconds(5000);
    cmd_.priority = 5;
    cmd_.expects_response = true;
    cmd_.return_type = "float";
    cmd_.channel_group = "analog";
    cmd_.channel_number = 1;
    cmd_.created_at = std::chrono::steady_clock::now();
  }

  SerializedCommand cmd_;
};

TEST_F(SerializedCommandTest, ToJsonBasic) {
  auto json = cmd_.to_json();

  EXPECT_EQ(json["id"], "test-123");
  EXPECT_EQ(json["instrument_name"], "DMM1");
  EXPECT_EQ(json["verb"], "MEASURE_VOLTAGE");
  EXPECT_EQ(json["priority"], 5);
  EXPECT_TRUE(json["expects_response"]);
  EXPECT_EQ(json["return_type"], "float");
  EXPECT_EQ(json["channel_group"], "analog");
  EXPECT_EQ(json["channel_number"], 1);
}

TEST_F(SerializedCommandTest, ParamsAllTypes) {
  cmd_.params["int_val"] = static_cast<int64_t>(42);
  cmd_.params["uint_val"] = static_cast<uint64_t>(100);
  cmd_.params["double_val"] = 3.14159;
  cmd_.params["bool_val"] = true;
  cmd_.params["string_val"] = std::string("hello");
  cmd_.params["array_double"] = std::vector<double>{1.0, 2.0, 3.0};
  cmd_.params["array_int"] = std::vector<int32_t>{1, 2, 3};

  auto json = cmd_.to_json();

  EXPECT_EQ(json["params"]["int_val"], 42);
  EXPECT_EQ(json["params"]["uint_val"], 100);
  EXPECT_NEAR(json["params"]["double_val"].get<double>(), 3.14159, 1e-5);
  EXPECT_TRUE(json["params"]["bool_val"]);
  EXPECT_EQ(json["params"]["string_val"], "hello");
  EXPECT_EQ(json["params"]["array_double"].size(), 3);
  EXPECT_EQ(json["params"]["array_int"][0], 1);
}

TEST_F(SerializedCommandTest, SerializationRoundTrip) {
  cmd_.params["voltage"] = 3.3;
  cmd_.params["samples"] = static_cast<int64_t>(1000);

  std::string serialized = ipc::serialize_command(cmd_);
  SerializedCommand deserialized = ipc::deserialize_command(serialized);

  EXPECT_EQ(deserialized.id, cmd_.id);
  EXPECT_EQ(deserialized.instrument_name, cmd_.instrument_name);
  EXPECT_EQ(deserialized.verb, cmd_.verb);
  EXPECT_EQ(deserialized.timeout.count(), cmd_.timeout.count());
  EXPECT_EQ(deserialized.priority, cmd_.priority);
  EXPECT_EQ(deserialized.expects_response, cmd_.expects_response);
  EXPECT_EQ(*deserialized.return_type, *cmd_.return_type);
  EXPECT_EQ(*deserialized.channel_group, *cmd_.channel_group);
  EXPECT_EQ(*deserialized.channel_number, *cmd_.channel_number);

  EXPECT_DOUBLE_EQ(std::get<double>(deserialized.params["voltage"]), 3.3);
  EXPECT_EQ(std::get<int64_t>(deserialized.params["samples"]), 1000);
}

TEST_F(SerializedCommandTest, EmptyParams) {
  std::string serialized = ipc::serialize_command(cmd_);
  SerializedCommand deserialized = ipc::deserialize_command(serialized);

  EXPECT_TRUE(deserialized.params.empty());
}

TEST_F(SerializedCommandTest, NoOptionalFields) {
  cmd_.return_type = std::nullopt;
  cmd_.channel_group = std::nullopt;
  cmd_.channel_number = std::nullopt;

  auto json = cmd_.to_json();

  EXPECT_FALSE(json.contains("return_type"));
  EXPECT_FALSE(json.contains("channel_group"));
  EXPECT_FALSE(json.contains("channel_number"));
}

class CommandResponseTest : public ::testing::Test {
protected:
  void SetUp() override {
    resp_.command_id = "test-456";
    resp_.instrument_name = "SCOPE1";
    resp_.success = true;
    resp_.error_code = 0;
    resp_.error_message = "";
    resp_.started = std::chrono::steady_clock::now();
    resp_.finished = resp_.started + std::chrono::milliseconds(100);
  }

  CommandResponse resp_;
};

TEST_F(CommandResponseTest, ToJsonSuccess) {
  resp_.text_response = "3.142";
  resp_.return_value = 3.142;

  auto json = resp_.to_json();

  EXPECT_EQ(json["command_id"], "test-456");
  EXPECT_EQ(json["instrument_name"], "SCOPE1");
  EXPECT_TRUE(json["success"]);
  EXPECT_EQ(json["error_code"], 0);
  EXPECT_EQ(json["text_response"], "3.142");
  EXPECT_NEAR(json["return_value"].get<double>(), 3.142, 1e-3);
}

TEST_F(CommandResponseTest, ToJsonFailure) {
  resp_.success = false;
  resp_.error_code = -100;
  resp_.error_message = "Device timeout";

  auto json = resp_.to_json();

  EXPECT_FALSE(json["success"]);
  EXPECT_EQ(json["error_code"], -100);
  EXPECT_EQ(json["error_message"], "Device timeout");
}

TEST_F(CommandResponseTest, DurationCalculation) {
  auto duration = resp_.duration();
  EXPECT_EQ(duration.count(), 100000); // 100ms in microseconds
}

TEST_F(CommandResponseTest, SerializationRoundTrip) {
  resp_.return_value = std::vector<double>{1.1, 2.2, 3.3};
  resp_.text_response = "OK";

  std::string serialized = ipc::serialize_response(resp_);
  CommandResponse deserialized = ipc::deserialize_response(serialized);

  EXPECT_EQ(deserialized.command_id, resp_.command_id);
  EXPECT_EQ(deserialized.instrument_name, resp_.instrument_name);
  EXPECT_EQ(deserialized.success, resp_.success);
  EXPECT_EQ(deserialized.text_response, resp_.text_response);

  auto arr = std::get<std::vector<double>>(*deserialized.return_value);
  EXPECT_EQ(arr.size(), 3);
  EXPECT_DOUBLE_EQ(arr[0], 1.1);
}
