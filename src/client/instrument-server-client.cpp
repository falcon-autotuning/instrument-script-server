#include "instrument-script-server/client/instrument-server-client.hpp"
#include <cstdint>

namespace instserver::client {

namespace v1 = instserver::daemon::v1;

/* ============================================================
 * Constructor
 * ============================================================ */

InstrumentServerClient::InstrumentServerClient(uint16_t port) {
  std::string addr = "127.0.0.1:" + std::to_string(port);

  channel_ = grpc::CreateChannel(addr, grpc::InsecureChannelCredentials());
  stub_ = v1::DaemonService::NewStub(channel_);
}

/* ============================================================
 * Health
 * ============================================================ */

bool InstrumentServerClient::is_daemon_running() {
  try {
    v1::DaemonStatusRequest req;
    auto resp = daemon_status(req);
    return resp.running();
  } catch (...) {
    return false;
  }
}

/* ============================================================
 * RPCs
 * ============================================================ */

v1::DaemonStatusResponse
InstrumentServerClient::daemon_status(const v1::DaemonStatusRequest &req) {

  return call_rpc(&v1::DaemonService::Stub::DaemonStatus, req);
}

/* ========================= */

v1::StartInstrumentResponse InstrumentServerClient::start_instrument(
    const v1::StartInstrumentRequest &req) {

  return call_rpc(&v1::DaemonService::Stub::StartInstrument, req);
}

/* ========================= */

v1::StopInstrumentResponse
InstrumentServerClient::stop_instrument(const v1::StopInstrumentRequest &req) {

  return call_rpc(&v1::DaemonService::Stub::StopInstrument, req);
}

/* ========================= */

v1::InstrumentStatusResponse InstrumentServerClient::instrument_status(
    const v1::InstrumentStatusRequest &req) {

  return call_rpc(&v1::DaemonService::Stub::InstrumentStatus, req);
}

/* ========================= */

v1::ListInstrumentsResponse InstrumentServerClient::list_instruments(
    const v1::ListInstrumentsRequest &req) {

  return call_rpc(&v1::DaemonService::Stub::ListInstruments, req);
}

/* ========================= */

v1::MeasureJobResponse
InstrumentServerClient::measure_job(const v1::MeasureJobRequest &req) {

  return call_rpc(&v1::DaemonService::Stub::MeasureJob, req);
}

/* ========================= */

v1::JobStatusResponse
InstrumentServerClient::job_status(const v1::JobStatusRequest &req) {

  return call_rpc(&v1::DaemonService::Stub::JobStatus, req);
}

/* ========================= */

v1::MeasureJobResultResponse InstrumentServerClient::measure_job_result(
    const v1::MeasureJobResultRequest &req) {

  return call_rpc(&v1::DaemonService::Stub::MeasureJobResult, req);
}

/* ========================= */

v1::JobListResponse
InstrumentServerClient::job_list(const v1::JobListRequest &req) {

  return call_rpc(&v1::DaemonService::Stub::JobList, req);
}

/* ========================= */

v1::CancelJobResponse
InstrumentServerClient::cancel_job(const v1::CancelJobRequest &req) {

  return call_rpc(&v1::DaemonService::Stub::CancelJob, req);
}

/* ========================= */

v1::DiscoverResponse
InstrumentServerClient::discover(const v1::DiscoverRequest &req) {

  return call_rpc(&v1::DaemonService::Stub::Discover, req);
}

/* ========================= */

v1::ListDataBuffersResponse InstrumentServerClient::list_data_buffers(
    const v1::ListDataBuffersRequest &req) {

  return call_rpc(&v1::DaemonService::Stub::ListDataBuffers, req);
}

/* ========================= */

v1::ReleaseBufferResponse
InstrumentServerClient::release_buffer(const v1::ReleaseBufferRequest &req) {

  return call_rpc(&v1::DaemonService::Stub::ReleaseBuffer, req);
}

/* ========================= */

v1::GetBufferMetadataResponse InstrumentServerClient::get_buffer_metadata(
    const v1::GetBufferMetadataRequest &req) {

  return call_rpc(&v1::DaemonService::Stub::GetBufferMetadata, req);
}

/* ========================= */

v1::StandardResponse
InstrumentServerClient::stop_daemon(const v1::DaemonStop &req) {

  return call_rpc(&v1::DaemonService::Stub::StopDaemon, req);
}

} // namespace instserver::client
