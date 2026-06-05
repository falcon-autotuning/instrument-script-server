#include "instrument-script-server/SerializedCommand.hpp"
#include "instrument-script-server/ipc/IPCMessage.hpp"

#include <cstring>

namespace instserver::ipc {

// =========================
// Helpers
// =========================

static void copy_string(char *dst, size_t dst_size, const std::string &src) {
  std::strncpy(dst, src.c_str(), dst_size - 1);
  dst[dst_size - 1] = '\0';
}
static std::string safe_string(const char *src, size_t max_len) {
  return {src, strnlen(src, max_len)};
}

void fill_ipc_command(IPCCommand &out, const SerializedCommand &in) {
  std::memset(&out, 0, sizeof(IPCCommand));

  copy_string(out.command_id, sizeof(out.command_id), in.id);
  copy_string(out.instrument_name, sizeof(out.instrument_name),
              in.instrument_name);
  copy_string(out.verb, sizeof(out.verb), in.verb);

  out.expects_response = in.expects_response;
  out.timeout_ms = static_cast<uint32_t>(in.timeout.count());
  out.sync_token = in.sync_token.value_or(0);

  out.param_count = 0;
  for (uint8_t i = 0; i < in.param_count; ++i) {
    const auto &src = in.params[i];

    if (out.param_count >= PLUGIN_MAX_PARAMS) {
      break;
    }

    auto &dst = out.params[out.param_count++];

    copy_string(dst.name, sizeof(dst.name), src.name);

    const auto &value = src.value;

    dst.value.type = static_cast<IPCParamValue::Type>(value.type);

    switch (value.type) {
    case ipc::IPCParamValue::Type::DOUBLE:
      dst.value.d = value.d;
      break;
    case ipc::IPCParamValue::Type::INT64:
      dst.value.i = value.i;
      break;
    case ipc::IPCParamValue::Type::BOOL:
      dst.value.b = value.b;
      break;
    case ipc::IPCParamValue::Type::STRING:
      copy_string(dst.value.str, sizeof(dst.value.str), value.str);
      break;
    case ipc::IPCParamValue::Type::DOUBLE_ARRAY: {
      size_t n = std::min(value.arr.size(), (size_t)PLUGIN_MAX_ARRAY_LEN);
      dst.value.arr.size = static_cast<uint32_t>(n);
      for (size_t j = 0; j < n; ++j) {
        dst.value.arr.data[j] = value.arr[j];
      }
      break;
    }
    }
  }
}

SerializedCommand from_ipc_command(const IPCCommand &in) {
  SerializedCommand out;

  out.id = safe_string(in.command_id, PLUGIN_MAX_STRING_LEN);
  out.instrument_name = safe_string(in.instrument_name, PLUGIN_MAX_STRING_LEN);
  out.verb = safe_string(in.verb, PLUGIN_MAX_STRING_LEN);

  out.expects_response = in.expects_response;
  out.timeout = std::chrono::milliseconds(in.timeout_ms);
  out.created_at = std::chrono::steady_clock::now();

  if (in.sync_token != 0) {
    out.sync_token = in.sync_token;
  }

  out.param_count = 0;

  uint8_t count = std::min<uint8_t>(in.param_count, PLUGIN_MAX_PARAMS);

  for (uint8_t i = 0; i < count; ++i) {
    const auto &src = in.params[i];

    if (src.value.type > IPCParamValue::Type::DOUBLE_ARRAY) {
      continue;
    }

    std::string key = safe_string(src.name, PLUGIN_MAX_STRING_LEN);
    if (key.empty()) {
      continue;
    }

    if (out.param_count >= PLUGIN_MAX_PARAMS) {
      break;
    }

    auto &dst = out.params[out.param_count++];

    dst.name = std::move(key);

    dst.value.type = src.value.type;

    switch (src.value.type) {
    case IPCParamValue::Type::DOUBLE:
      dst.value.d = src.value.d;
      break;
    case IPCParamValue::Type::INT64:
      dst.value.i = src.value.i;
      break;
    case IPCParamValue::Type::BOOL:
      dst.value.b = src.value.b;
      break;
    case IPCParamValue::Type::STRING:
      dst.value.str = safe_string(src.value.str, PLUGIN_MAX_STRING_LEN);
      break;
    case IPCParamValue::Type::DOUBLE_ARRAY: {
      uint32_t n = std::min<uint32_t>(src.value.arr.size, PLUGIN_MAX_ARRAY_LEN);
      dst.value.arr.assign(src.value.arr.data, src.value.arr.data + n);
      break;
    }
    }
  }

  return out;
}

void fill_ipc_response(IPCResponse &out, const CommandResponse &in) {
  std::memset(&out, 0, sizeof(IPCResponse));

  copy_string(out.command_id, sizeof(out.command_id), in.command_id);
  copy_string(out.instrument_name, sizeof(out.instrument_name),
              in.instrument_name);

  out.success = in.success;
  out.error_code = in.error_code;

  copy_string(out.error_message, sizeof(out.error_message), in.error_message);
  copy_string(out.text_response, sizeof(out.text_response), in.text_response);

  // Return value
  out.has_return_value = in.return_value.has_value();

  if (in.return_value) {
    const auto &v = *in.return_value;

    out.return_value.type = static_cast<IPCParamValue::Type>(v.type);

    switch (v.type) {
    case ipc::IPCParamValue::Type::DOUBLE:
      out.return_value.d = v.d;
      break;
    case ipc::IPCParamValue::Type::INT64:
      out.return_value.i = v.i;
      break;
    case ipc::IPCParamValue::Type::BOOL:
      out.return_value.b = v.b;
      break;
    case ipc::IPCParamValue::Type::STRING:
      copy_string(out.return_value.str, sizeof(out.return_value.str), v.str);
      break;
    case ipc::IPCParamValue::Type::DOUBLE_ARRAY: {
      size_t n = std::min(v.arr.size(), (size_t)PLUGIN_MAX_ARRAY_LEN);
      out.return_value.arr.size = static_cast<uint32_t>(n);
      for (size_t i = 0; i < n; ++i) {
        out.return_value.arr.data[i] = v.arr[i];
      }
      break;
    }
    }
  }

  // Large data fields
  out.has_large_data = in.has_large_data;

  if (in.has_large_data) {
    copy_string(out.buffer_id, sizeof(out.buffer_id), in.buffer_id);
    out.element_count = static_cast<uint32_t>(in.element_count);
    copy_string(out.data_type, sizeof(out.data_type), in.data_type);
  }
}

CommandResponse from_ipc_response(const IPCResponse &in) {
  CommandResponse out;

  out.command_id = safe_string(in.command_id, PLUGIN_MAX_STRING_LEN);
  out.instrument_name = safe_string(in.instrument_name, PLUGIN_MAX_STRING_LEN);

  out.success = in.success;
  out.error_code = in.error_code;

  out.error_message = safe_string(in.error_message, PLUGIN_MAX_STRING_LEN);
  out.text_response = safe_string(in.text_response, PLUGIN_MAX_STRING_LEN);

  if (in.has_return_value) {
    if (in.return_value.type > IPCParamValue::Type::DOUBLE_ARRAY) {
      return out; // invalid → ignore
    }

    ParamValue v;
    v.type = static_cast<ParamType>(in.return_value.type);

    switch (in.return_value.type) {
    case IPCParamValue::Type::DOUBLE:
      v.d = in.return_value.d;
      break;

    case IPCParamValue::Type::INT64:
      v.i = in.return_value.i;
      break;

    case IPCParamValue::Type::BOOL:
      v.b = in.return_value.b;
      break;

    case IPCParamValue::Type::STRING:
      v.str = safe_string(in.return_value.str, PLUGIN_MAX_STRING_LEN);
      break;

    case IPCParamValue::Type::DOUBLE_ARRAY: {
      uint32_t n =
          std::min<uint32_t>(in.return_value.arr.size, PLUGIN_MAX_ARRAY_LEN);

      v.arr.assign(in.return_value.arr.data, in.return_value.arr.data + n);
      break;
    }
    }

    out.return_value = std::move(v);
  }

  out.has_large_data = in.has_large_data;

  if (in.has_large_data) {
    out.buffer_id = safe_string(in.buffer_id, PLUGIN_MAX_STRING_LEN);
    out.data_type = safe_string(in.data_type, sizeof(in.data_type));
    out.element_count = in.element_count;
  }

  return out;
}

} // namespace instserver::ipc
