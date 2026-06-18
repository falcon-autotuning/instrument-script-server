#include "instrument-script-server/ipc/SharedQueue.hpp"
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/interprocess/creation_tags.hpp>
#include <instrument-log/inst_logging.h>
#include <memory>
#include <string>

namespace instserver::ipc {
namespace {
std::string make_queue_name(const std::string &instrument_name,
                            const std::string &suffix) {
  return "instrument_" + instrument_name + "_" + suffix;
}
} // namespace

std::unique_ptr<SharedQueue>
SharedQueue::create_server_queue(const std::string &instrument_name) {
  using namespace boost::interprocess;

  std::string req_name = make_queue_name(instrument_name, "req");
  std::string resp_name = make_queue_name(instrument_name, "resp");

  // Remove existing queues if any
  message_queue::remove(req_name.c_str());
  message_queue::remove(resp_name.c_str());

  try {
    auto req_queue = std::make_unique<message_queue>(
        create_only, req_name.c_str(), MAX_QUEUED_MESSAGES, sizeof(IPCMessage));

    auto resp_queue = std::make_unique<message_queue>(
        create_only, resp_name.c_str(), MAX_QUEUED_MESSAGES,
        sizeof(IPCMessage));

    LOG_INFO("IPC", "QUEUE_CREATE", "Created queues for instrument: %s",
             instrument_name.c_str());

    // SERVER:   sends on request, receives on response
    return std::make_unique<SharedQueue>(
        std::move(req_queue), std::move(resp_queue), req_name, resp_name, true);
  } catch (const interprocess_exception &ex) {
    LOG_ERROR("IPC", "QUEUE_CREATE", "Failed to create queues: %s", ex.what());
    throw;
  }
}

std::unique_ptr<SharedQueue>
SharedQueue::create_worker_queue(const std::string &instrument_name) {
  using namespace boost::interprocess;

  std::string req_name = make_queue_name(instrument_name, "req");
  std::string resp_name = make_queue_name(instrument_name, "resp");

  try {
    auto req_queue =
        std::make_unique<message_queue>(open_only, req_name.c_str());

    auto resp_queue =
        std::make_unique<message_queue>(open_only, resp_name.c_str());

    LOG_INFO("IPC", "QUEUE_OPEN", "Opened queues for instrument: %s",
             instrument_name.c_str());

    // WORKER:  receives on request, sends on response
    return std::make_unique<SharedQueue>(std::move(req_queue),
                                         std::move(resp_queue), req_name,
                                         resp_name, false);
  } catch (const interprocess_exception &ex) {
    LOG_ERROR("IPC", "QUEUE_OPEN", "Failed to open queues: %s", ex.what());
    throw;
  }
}

instserver::ipc::SharedQueue::SharedQueue(
    std::unique_ptr<boost::interprocess::message_queue> req_queue,
    std::unique_ptr<boost::interprocess::message_queue> resp_queue,
    std::string req_name, std::string resp_name, bool is_server)
    : request_queue_(std::move(req_queue)),
      response_queue_(std::move(resp_queue)),
      request_queue_name_(std::move(req_name)),
      response_queue_name_(std::move(resp_name)), is_server_(is_server) {}

instserver::ipc::SharedQueue::~SharedQueue() = default;
// Queues are automatically closed when unique_ptr is destroyed

bool instserver::ipc::SharedQueue::send(const IPCMessage &msg,
                                        std::chrono::milliseconds timeout) {
  if (!is_valid()) {
    return false;
  }

  try {
    auto abs_time = boost::posix_time::microsec_clock::universal_time() +
                    boost::posix_time::milliseconds(timeout.count());

    // Server sends on request queue, worker sends on response queue
    auto *queue = is_server_ ? request_queue_.get() : response_queue_.get();
    bool sent = queue->timed_send(&msg, sizeof(msg), 0, abs_time);

    if (!sent) {
      const std::string &queue_name =
          is_server_ ? request_queue_name_ : response_queue_name_;
      LOG_WARN("IPC", "SEND_TIMEOUT",
               "Send timeout (%dms) on queue '%s' msg_id=%s type=%u",
               (int)timeout.count(), queue_name.c_str(), msg.id.data(),
               (unsigned int)msg.type);
    }

    return sent;
  } catch (const boost::interprocess::interprocess_exception &ex) {
    LOG_ERROR("IPC", "SEND_ERROR", "Send failed: %s", ex.what());
    return false;
  }
}

std::optional<IPCMessage>
SharedQueue::receive(std::chrono::milliseconds timeout) {
  if (!is_valid()) {
    return std::nullopt;
  }

  try {
    IPCMessage msg;
    size_t received_size = 0;
    unsigned int priority = 0;

    auto abs_time = boost::posix_time::microsec_clock::universal_time() +
                    boost::posix_time::milliseconds(timeout.count());

    // Server receives on response queue, worker receives on request queue
    auto *queue = is_server_ ? response_queue_.get() : request_queue_.get();
    bool received = queue->timed_receive(&msg, sizeof(msg), received_size,
                                         priority, abs_time);

    if (!received) {
      LOG_TRACE("IPC", "RECV_TIMEOUT", "Receive timeout on queue: %s",
                response_queue_name_.c_str());
      return std::nullopt;
    }

    if (received_size != sizeof(IPCMessage)) {
      LOG_ERROR("IPC", "RECV_SIZE", "Received message size mismatch: %d vs %d",
                received_size, sizeof(IPCMessage));
      return std::nullopt;
    }

    return msg;
  } catch (const boost::interprocess::interprocess_exception &ex) {
    LOG_ERROR("IPC", "RECV_ERROR", "Receive failed:  %s", ex.what());
    return std::nullopt;
  }
}

void SharedQueue::cleanup(const std::string &instrument_name) {
  using namespace boost::interprocess;

  std::string req_name = make_queue_name(instrument_name, "req");
  std::string resp_name = make_queue_name(instrument_name, "resp");

  message_queue::remove(req_name.c_str());
  message_queue::remove(resp_name.c_str());

  LOG_INFO("IPC", "QUEUE_CLEANUP", "Cleaned up queues for: %s",
           instrument_name.c_str());
}

bool SharedQueue::receive_blocking(IPCMessage &msg) {
  if (!is_valid()) {
    return false;
  }

  try {
    size_t received_size = 0;
    unsigned int priority = 0;

    // Server receives on response queue, worker receives on request queue
    auto *queue = is_server_ ? response_queue_.get() : request_queue_.get();

    queue->receive(&msg, sizeof(msg), received_size, priority);

    if (received_size != sizeof(IPCMessage)) {
      LOG_ERROR("IPC", "RECV_SIZE",
                "Received message size mismatch: %zu vs %zu", received_size,
                sizeof(IPCMessage));
      return false;
    }

    return true;

  } catch (const boost::interprocess::interprocess_exception &ex) {
    LOG_ERROR("IPC", "RECV_ERROR", "Receive failed: %s", ex.what());
    return false;
  }
}
bool SharedQueue::send_to_response_queue(const IPCMessage &msg,
                                         std::chrono::milliseconds timeout) {
  if (!is_valid()) {
    return false;
  }

  try {
    auto abs_time = boost::posix_time::microsec_clock::universal_time() +
                    boost::posix_time::milliseconds(timeout.count());

    return response_queue_->timed_send(&msg, sizeof(msg), 0, abs_time);

  } catch (const boost::interprocess::interprocess_exception &ex) {
    LOG_ERROR("IPC", "SEND_ERROR", "Send failed: %s", ex.what());
    return false;
  }
}

} // namespace instserver::ipc
