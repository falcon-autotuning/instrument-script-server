#include "instrument-script-server/core/InstrumentCommand.hpp"
#include "instrument-script-server/ipc/IPCMessage.hpp"

#include <cstring>
#include <inst_logging.h>
#include <instrument-plugin.h>

namespace instserver::ipc {

// =========================
// Helpers
// =========================

static void copy_string(char *dst, size_t dst_size, const std::string &src) {
  std::strncpy(dst, src.c_str(), dst_size - 1);
  dst[dst_size - 1] = '\0';
}
static std::string safe_string(const char *src, size_t max_len) {
  if (src == nullptr) {
    return {};
  }

  // Stop at null *without* reading out-of-bounds
  size_t len = 0;
  while (len < max_len) {
    if (src[len] == '\0') {
      break;
    }
    ++len;
  }

  return {src, len};
}

void fill_ipc_commands(std::vector<IPCMessage> &out,
                       const InstrumentCommand &in) {

  const size_t total = in.params.size();
  const size_t num_chunks =
      std::max<size_t>(1, (total + PARAM_CHUNK - 1) / PARAM_CHUNK);

  out.reserve(out.size() + num_chunks);

  IPCMessage base{};
  base.type = IPCMessage::Type::COMMAND;
  new (&base.command) IPCCommand{};

  copy_string(base.id.data(), base.id.size(), in.id);
  base.sync_token = in.sync_token.value_or(0);

  auto &cmd = base.command;

  copy_string(cmd.command.data(), cmd.command.size(), in.verb);

  cmd.timeout_ms = static_cast<uint32_t>(in.timeout.count());
  cmd.param_total = static_cast<uint16_t>(total);

  for (size_t chunk = 0; chunk < num_chunks; ++chunk) {
    IPCMessage msg = base;

    auto &c = msg.command;

    size_t start = chunk * PARAM_CHUNK;
    size_t end = std::min(start + PARAM_CHUNK, total);

    c.param_count = static_cast<uint8_t>(end - start);

    for (size_t i = start; i < end; ++i) {
      c.params[i - start] = in.params[i];
    }

    out.push_back(msg);
  }
}
InstrumentCommand from_ipc_commands(const std::vector<IPCMessage> &in_vec) {
  InstrumentCommand out;

  if (in_vec.empty()) {
    return out;
  }

  const IPCMessage &first = in_vec.front();

  if (first.type != IPCMessage::Type::COMMAND) {
    return out;
  }

  out.id = safe_string(first.id.data(), PLUGIN_MAX_STRING_LEN);
  out.verb = safe_string(first.command.command.data(), PLUGIN_MAX_STRING_LEN);

  if (first.sync_token != 0) {
    out.sync_token = first.sync_token;
  }

  out.timeout = std::chrono::milliseconds(first.command.timeout_ms);
  out.created_at = std::chrono::steady_clock::now();

  size_t total = first.command.param_total;
  out.params.reserve(total);

  for (const auto &msg : in_vec) {
    if (msg.type != IPCMessage::Type::COMMAND) {
      continue;
    }

    const auto &cmd = msg.command;

    for (uint8_t i = 0; i < cmd.param_count; ++i) {
      out.params.push_back(cmd.params[i]);
    }
  }

  return out;
}

void fill_ipc_responses(std::vector<IPCMessage> &out,
                        const InstrumentCommandResponse &in) {

  size_t total = in.returns.size();
  size_t num_chunks =
      std::max<size_t>(1, (total + PARAM_CHUNK - 1) / PARAM_CHUNK);

  out.reserve(out.size() + num_chunks);

  IPCMessage base{};
  base.type = IPCMessage::Type::RESPONSE;
  new (&base.response) IPCResponse{};

  copy_string(base.id.data(), base.id.size(), in.id);
  base.response.error_code = static_cast<int8_t>(in.error_code);
  base.response.return_total = static_cast<uint16_t>(total);

  for (size_t chunk = 0; chunk < num_chunks; ++chunk) {
    IPCMessage msg = base;

    size_t start = chunk * PARAM_CHUNK;
    size_t end = std::min(start + PARAM_CHUNK, total);

    msg.response.return_count = static_cast<uint8_t>(end - start);

    for (size_t i = start; i < end; ++i) {
      msg.response.returns[i - start] = in.returns[i];
    }

    out.push_back(msg);
  }
}

InstrumentCommandResponse
from_ipc_responses(const std::vector<IPCMessage> &in_vec) {
  InstrumentCommandResponse out;

  if (in_vec.empty()) {
    return out;
  }

  const IPCMessage &first = in_vec.front();
  if (first.type != IPCMessage::Type::RESPONSE) {
    return out;
  }

  out.id = safe_string(first.id.data(), first.id.size());

  const auto &resp0 = first.response;
  out.error_code = ErrorCode(resp0.error_code);

  size_t total = resp0.return_total;
  out.returns.reserve(total);

  size_t accumulated = 0;

  for (const auto &msg : in_vec) {
    if (msg.type != IPCMessage::Type::RESPONSE)
      continue;

    uint8_t count = msg.response.return_count;

    if (count > PARAM_CHUNK) {
      LOG_ERROR("IPC", "RESPONSE", "Invalid return_count=%u", count);
      continue;
    }

    accumulated += count;

    for (uint8_t i = 0; i < count; ++i) {
      out.returns.push_back(msg.response.returns[i]);
    }
  }

  if (accumulated != total) {
    LOG_ERROR("IPC", "RESPONSE", "Mismatch total=%zu accumulated=%zu", total,
              accumulated);
    out.returns.clear(); // prevent downstream crash
  }

  return out;
}

} // namespace instserver::ipc
