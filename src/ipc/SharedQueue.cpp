#include "instrument-server/ipc/SharedQueue.hpp"
#include "instrument-server/Logger.hpp"
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/interprocess/creation_tags.hpp>
#include <memory>
#include <string>

namespace instserver {
namespace ipc {

static std::string make_queue_name(const std::string &instrument_name,
                                   const std::string &suffix) {
  return "instrument_" + instrument_name + "_" + suffix;
}

std::unique_ptr<SharedQueue>
SharedQueue::create_server_queue(const std::string &instrument_name) {
  using namespace boost::interprocess;

  std::string req_name = make_queue_name(instrument_name, "req");
  std::string resp_name = make_queue_name(instrument_name, "resp");

  // Remove existing queues if any
  message_queue::remove(req_name.c_str());
  message_queue::remove(resp_name.c_str());

  try {
    auto req_queue =
        std::make_unique<message_queue>(create_only, req_name.c_str(),
                                        100, // max messages
                                        sizeof(IPCMessage));

    auto resp_queue = std::make_unique<message_queue>(
        create_only, resp_name.c_str(), 100, sizeof(IPCMessage));

    LOG_INFO("IPC", "QUEUE_CREATE", "Created queues for instrument: {}",
             instrument_name);

    // SERVER:   sends on request, receives on response
    return std::make_unique<SharedQueue>(
        std::move(req_queue), std::move(resp_queue), req_name, resp_name, true);
  } catch (const interprocess_exception &ex) {
    LOG_ERROR("IPC", "QUEUE_CREATE", "Failed to create queues:  {}", ex.what());
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

    LOG_INFO("IPC", "QUEUE_OPEN", "Opened queues for instrument:  {}",
             instrument_name);

    // WORKER:  receives on request, sends on response
    return std::make_unique<SharedQueue>(std::move(req_queue),
                                         std::move(resp_queue), req_name,
                                         resp_name, false);
  } catch (const interprocess_exception &ex) {
    LOG_ERROR("IPC", "QUEUE_OPEN", "Failed to open queues: {}", ex.what());
    throw;
  }
}

instserver::ipc::SharedQueue::SharedQueue(
    std::unique_ptr<boost::interprocess::message_queue> req_queue,
    std::unique_ptr<boost::interprocess::message_queue> resp_queue,
    const std::string &req_name, const std::string &resp_name, bool is_server)
    : request_queue_(std::move(req_queue)),
      response_queue_(std::move(resp_queue)), request_queue_name_(req_name),
      response_queue_name_(resp_name), is_server_(is_server) {}

instserver::ipc::SharedQueue::~SharedQueue() {
  // Queues are automatically closed when unique_ptr is destroyed
}

bool instserver::ipc::SharedQueue::send(const IPCMessage &msg,
                                        std::chrono::milliseconds timeout) {
  if (!is_valid())
    return false;

  try {
    auto abs_time = boost::posix_time::microsec_clock::universal_time() +
                    boost::posix_time::milliseconds(timeout.count());

    // Server sends on request queue, worker sends on response queue
    auto *queue = is_server_ ? request_queue_.get() : response_queue_.get();
    bool sent = queue->timed_send(&msg, sizeof(msg), 0, abs_time);

    if (!sent) {
      LOG_WARN("IPC", "SEND_TIMEOUT", "Send timeout on queue: {}",
               request_queue_name_);
    }

    return sent;
  } catch (const boost::interprocess::interprocess_exception &ex) {
    LOG_ERROR("IPC", "SEND_ERROR", "Send failed: {}", ex.what());
    return false;
  }
}

std::optional<IPCMessage>
SharedQueue::receive(std::chrono::milliseconds timeout) {
  if (!is_valid())
    return std::nullopt;

  try {
    IPCMessage msg;
    size_t received_size;
    unsigned int priority;

    auto abs_time = boost::posix_time::microsec_clock::universal_time() +
                    boost::posix_time::milliseconds(timeout.count());

    // Server receives on response queue, worker receives on request queue
    auto *queue = is_server_ ? response_queue_.get() : request_queue_.get();
    bool received = queue->timed_receive(&msg, sizeof(msg), received_size,
                                         priority, abs_time);

    if (!received) {
      LOG_TRACE("IPC", "RECV_TIMEOUT", "Receive timeout on queue: {}",
                response_queue_name_);
      return std::nullopt;
    }

    if (received_size != sizeof(IPCMessage)) {
      LOG_ERROR("IPC", "RECV_SIZE", "Received message size mismatch: {} vs {}",
                received_size, sizeof(IPCMessage));
      return std::nullopt;
    }

    return msg;
  } catch (const boost::interprocess::interprocess_exception &ex) {
    LOG_ERROR("IPC", "RECV_ERROR", "Receive failed:  {}", ex.what());
    return std::nullopt;
  }
}

void SharedQueue::cleanup(const std::string &instrument_name) {
  using namespace boost::interprocess;

  std::string req_name = make_queue_name(instrument_name, "req");
  std::string resp_name = make_queue_name(instrument_name, "resp");

  message_queue::remove(req_name.c_str());
  message_queue::remove(resp_name.c_str());

  LOG_INFO("IPC", "QUEUE_CLEANUP", "Cleaned up queues for:   {}",
           instrument_name);
}

} // namespace ipc
} // namespace instserver
