#include "instrument-script-server/daemon/GrpcServer.hpp"
#include "instrument-script-server/daemon/CommandHandlers.hpp"
#include "instserver/daemon/v1/daemon_messages.grpc.pb.h"
#include <grpcpp/grpcpp.h>
#include <instrument-log/inst_logging.h>

namespace instserver::daemon {

class DaemonServiceImpl final : public v1::DaemonService::Service {
public:
  grpc::Status DaemonStatus(grpc::ServerContext *context,
                            const v1::DaemonStatusRequest *req,
                            v1::DaemonStatusResponse *resp) override {
    int rc = handle_daemon_status(*req, resp);
    return rc == 0 ? grpc::Status::OK
                   : grpc::Status(grpc::StatusCode::INTERNAL,
                                  "DaemonStatus failed");
  }

  grpc::Status StartInstrument(grpc::ServerContext *context,
                               const v1::StartInstrumentRequest *req,
                               v1::StartInstrumentResponse *resp) override {
    int rc = handle_start_instrument(*req, resp);
    return rc == 0 ? grpc::Status::OK
                   : grpc::Status(grpc::StatusCode::INTERNAL,
                                  "StartInstrument failed");
  }

  grpc::Status StopInstrument(grpc::ServerContext *context,
                              const v1::StopInstrumentRequest *req,
                              v1::StopInstrumentResponse *resp) override {
    int rc = handle_stop_instrument(*req, resp);
    return rc == 0 ? grpc::Status::OK
                   : grpc::Status(grpc::StatusCode::INTERNAL,
                                  "StopInstrument failed");
  }

  grpc::Status InstrumentStatus(grpc::ServerContext *context,
                                const v1::InstrumentStatusRequest *req,
                                v1::InstrumentStatusResponse *resp) override {
    int rc = handle_instrument_status(*req, resp);
    return rc == 0 ? grpc::Status::OK
                   : grpc::Status(grpc::StatusCode::INTERNAL,
                                  "InstrumentStatus failed");
  }

  grpc::Status ListInstruments(grpc::ServerContext *context,
                               const v1::ListInstrumentsRequest *req,
                               v1::ListInstrumentsResponse *resp) override {
    int rc = handle_list_instruments(*req, resp);
    return rc == 0 ? grpc::Status::OK
                   : grpc::Status(grpc::StatusCode::INTERNAL,
                                  "ListInstruments failed");
  }

  grpc::Status MeasureJob(grpc::ServerContext *context,
                          const v1::MeasureJobRequest *req,
                          v1::MeasureJobResponse *resp) override {
    int rc = handle_measure_job(*req, resp);
    return rc == 0
               ? grpc::Status::OK
               : grpc::Status(grpc::StatusCode::INTERNAL, "MeasureJob failed");
  }

  grpc::Status JobStatus(grpc::ServerContext *context,
                         const v1::JobStatusRequest *req,
                         v1::JobStatusResponse *resp) override {
    int rc = handle_job_status(*req, resp);
    return rc == 0
               ? grpc::Status::OK
               : grpc::Status(grpc::StatusCode::INTERNAL, "JobStatus failed");
  }

  grpc::Status MeasureJobResult(grpc::ServerContext *context,
                                const v1::MeasureJobResultRequest *req,
                                v1::MeasureJobResultResponse *resp) override {
    int rc = handle_measure_job_result(*req, resp);
    return rc == 0 ? grpc::Status::OK
                   : grpc::Status(grpc::StatusCode::INTERNAL,
                                  "MeasureJobResult failed");
  }

  grpc::Status JobList(grpc::ServerContext *context,
                       const v1::JobListRequest *req,
                       v1::JobListResponse *resp) override {
    int rc = handle_job_list(*req, resp);
    return rc == 0 ? grpc::Status::OK
                   : grpc::Status(grpc::StatusCode::INTERNAL, "JobList failed");
  }

  grpc::Status CancelJob(grpc::ServerContext *context,
                         const v1::CancelJobRequest *req,
                         v1::CancelJobResponse *resp) override {
    int rc = handle_cancel_job(*req, resp);
    return rc == 0
               ? grpc::Status::OK
               : grpc::Status(grpc::StatusCode::INTERNAL, "CancelJob failed");
  }

  grpc::Status Discover(grpc::ServerContext *context,
                        const v1::DiscoverRequest *req,
                        v1::DiscoverResponse *resp) override {
    int rc = handle_discover(*req, resp);
    return rc == 0
               ? grpc::Status::OK
               : grpc::Status(grpc::StatusCode::INTERNAL, "Discover failed");
  }

  grpc::Status ListDataBuffers(grpc::ServerContext *context,
                               const v1::ListDataBuffersRequest *req,
                               v1::ListDataBuffersResponse *resp) override {
    int rc = handle_list_buffers(*req, resp);
    return rc == 0 ? grpc::Status::OK
                   : grpc::Status(grpc::StatusCode::INTERNAL,
                                  "ListDataBuffers failed");
  }

  grpc::Status ReleaseBuffer(grpc::ServerContext *context,
                             const v1::ReleaseBufferRequest *req,
                             v1::ReleaseBufferResponse *resp) override {
    int rc = handle_release_buffer(*req, resp);
    return rc == 0 ? grpc::Status::OK
                   : grpc::Status(grpc::StatusCode::INTERNAL,
                                  "ReleaseBuffer failed");
  }

  grpc::Status GetBufferMetadata(grpc::ServerContext *context,
                                 const v1::GetBufferMetadataRequest *req,
                                 v1::GetBufferMetadataResponse *resp) override {
    int rc = handle_get_buffer_metadata(*req, resp);
    return rc == 0 ? grpc::Status::OK
                   : grpc::Status(grpc::StatusCode::INTERNAL,
                                  "GetBufferMetadata failed");
  }

  grpc::Status StopDaemon(grpc::ServerContext *context,
                          const v1::DaemonStop *req,
                          v1::StandardResponse *resp) override {
    int rc = handle_daemon_stop(*req, nullptr);
    if (resp) {
      resp->set_ok(rc == 0);
    }
    return rc == 0
               ? grpc::Status::OK
               : grpc::Status(grpc::StatusCode::INTERNAL, "StopDaemon failed");
  }
};

GrpcServer::GrpcServer() = default;
GrpcServer::~GrpcServer() { stop(); }

bool GrpcServer::start(uint16_t port) {
  if (running_.exchange(true)) {
    return true;
  }
  server_thread_ = std::thread(&GrpcServer::run_loop, this, port);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  return true;
}

void GrpcServer::stop() {
  if (!running_.exchange(false)) {
    return;
  }
  if (server_) {
    server_->Shutdown();
  }
  if (server_thread_.joinable()) {
    server_thread_.join();
  }
}

uint16_t GrpcServer::port() const { return bound_port_; }

void GrpcServer::run_loop(uint16_t port) {
  std::string server_address = "127.0.0.1:" + std::to_string(port);
  DaemonServiceImpl service;

  grpc::ServerBuilder builder;
  int selected_port = 0;
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials(),
                           &selected_port);
  builder.RegisterService(&service);

  server_ = builder.BuildAndStart();
  if (!server_) {
    LOG_ERROR("RPC", "START", "Failed to start gRPC server");
    running_ = false;
    return;
  }

  bound_port_ = static_cast<uint16_t>(selected_port);
  LOG_INFO("RPC", "START", "gRPC Server listening on 127.0.0.1:%d",
           bound_port_);

  server_->Wait();
}

} // namespace instserver::daemon
