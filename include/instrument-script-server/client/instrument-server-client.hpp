#pragma once

#include <memory>

#include "instserver/daemon/v1/daemon_messages.grpc.pb.h"
#include <grpcpp/grpcpp.h>

namespace instserver::client {

/**
 * @brief C++20 client for Instrument Script Server
 *
 * This is a thin wrapper around gRPC that exposes native protobuf types.
 *
 * ------------------------------------------------------------
 * Error Handling
 * ------------------------------------------------------------
 *
 * All methods throw std::runtime_error on RPC failure.
 *
 * ------------------------------------------------------------
 * Example
 * ------------------------------------------------------------
 *
 * @code
 * InstrumentServerClient client(50051);
 *
 * v1::JobListRequest req;
 * auto resp = client.job_list(req);
 *
 * for (const auto& [id, job] : resp.jobs()) {
 *     std::cout << id << std::endl;
 * }
 * @endcode
 */

namespace v1 = instserver::daemon::v1;

class InstrumentServerClient {
public:
  /**
   * @brief Construct client connected to given port
   */
  explicit InstrumentServerClient(uint16_t port);

  ~InstrumentServerClient();

  InstrumentServerClient(const InstrumentServerClient &) = delete;
  InstrumentServerClient &operator=(const InstrumentServerClient &) = delete;

  InstrumentServerClient(InstrumentServerClient &&) = default;
  InstrumentServerClient &operator=(InstrumentServerClient &&) = default;

  /* ========================= */
  /* Health */
  /* ========================= */

  /**
   * @brief Check if daemon is reachable
   */
  bool is_daemon_running();

  /* ========================= */
  /* RPCs */
  /* ========================= */

  v1::DaemonStatusResponse daemon_status(const v1::DaemonStatusRequest &req);

  v1::StartInstrumentResponse
  start_instrument(const v1::StartInstrumentRequest &req);

  v1::StopInstrumentResponse
  stop_instrument(const v1::StopInstrumentRequest &req);

  v1::InstrumentStatusResponse
  instrument_status(const v1::InstrumentStatusRequest &req);

  v1::ListInstrumentsResponse
  list_instruments(const v1::ListInstrumentsRequest &req);

  v1::MeasureJobResponse measure_job(const v1::MeasureJobRequest &req);

  v1::JobStatusResponse job_status(const v1::JobStatusRequest &req);

  v1::MeasureJobResultResponse
  measure_job_result(const v1::MeasureJobResultRequest &req);

  v1::JobListResponse job_list(const v1::JobListRequest &req);

  v1::CancelJobResponse cancel_job(const v1::CancelJobRequest &req);

  v1::DiscoverResponse discover(const v1::DiscoverRequest &req);

  v1::ListDataBuffersResponse
  list_data_buffers(const v1::ListDataBuffersRequest &req);

  v1::ReleaseBufferResponse release_buffer(const v1::ReleaseBufferRequest &req);

  v1::GetBufferMetadataResponse
  get_buffer_metadata(const v1::GetBufferMetadataRequest &req);

  v1::StandardResponse stop_daemon(const v1::DaemonStop &req);

private:
  std::shared_ptr<grpc::Channel> channel_;
  std::unique_ptr<v1::DaemonService::Stub> stub_;

  /**
   * @brief Helper for invoking RPC safely
   */

  template <typename Request, typename Response>
  Response call_rpc(grpc::Status (v1::DaemonService::Stub::*func)(
                        grpc::ClientContext *, const Request &, Response *),
                    const Request &req) {

    Response resp;
    grpc::ClientContext ctx;

    ctx.set_deadline(std::chrono::system_clock::now() +
                     std::chrono::seconds(5));

    grpc::Status status = (stub_.get()->*func)(&ctx, req, &resp);

    if (!status.ok()) {
      throw std::runtime_error("gRPC call failed: " + status.error_message());
    }

    return resp;
  }
};
} // namespace instserver::client
