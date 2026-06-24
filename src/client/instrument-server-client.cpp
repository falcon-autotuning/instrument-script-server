#include "instrument-script-server/instrument-script-server-client.h"

#include "instserver/server/v1/daemon_messages.grpc.pb.h"
#include <grpcpp/grpcpp.h>

#include <chrono>
#include <string>
#include <vector>

using namespace instserver::server;

struct instrument_server_client {
  std::shared_ptr<grpc::Channel> channel;
  std::unique_ptr<v1::DaemonService::Stub> stub;
};
namespace {
bool proto_c_to_cpp(const ProtobufCMessage *c_msg,
                    google::protobuf::Message &cpp_msg) {
  size_t size = protobuf_c_message_get_packed_size(c_msg);
  std::vector<uint8_t> buffer(size);

  protobuf_c_message_pack(c_msg, buffer.data());

  return cpp_msg.ParseFromArray(buffer.data(), (int)size);
}

ProtobufCMessage *proto_cpp_to_c(const google::protobuf::Message &cpp_msg,
                                 const ProtobufCMessageDescriptor *desc) {
  std::string bytes = cpp_msg.SerializeAsString();

  return protobuf_c_message_unpack(
      desc, nullptr, bytes.size(),
      reinterpret_cast<const uint8_t *>(bytes.data()));
}
} // namespace

extern "C" {

instrument_server_client_t *instrument_server_client_create(uint16_t port) {
  auto *client = new instrument_server_client;

  std::string addr = "127.0.0.1:" + std::to_string(port);

  client->channel =
      grpc::CreateChannel(addr, grpc::InsecureChannelCredentials());

  client->stub = v1::DaemonService::NewStub(client->channel);

  return client;
}

void instrument_server_client_destroy(instrument_server_client_t *client) {
  delete client;
}

int instrument_server_client_is_daemon_running(uint16_t port) {
  std::string addr = "127.0.0.1:" + std::to_string(port);
  auto channel =
      grpc::CreateChannel(addr, grpc::InsecureChannelCredentials());
  auto stub = v1::DaemonService::NewStub(channel);

  grpc::ClientContext ctx;
  // Use a short deadline so we fail fast when the daemon is not running
  ctx.set_deadline(std::chrono::system_clock::now() +
                   std::chrono::milliseconds(500));

  v1::DaemonStatusRequest req;
  v1::DaemonStatusResponse resp;
  auto status = stub->DaemonStatus(&ctx, req, &resp);

  if (!status.ok()) {
    return 0;
  }
  return resp.running() ? 1 : 0;
}

void instrument_server_client_free_response(void *msg) {
  if (!msg)
    return;
  protobuf_c_message_free_unpacked((ProtobufCMessage *)msg, nullptr);
}

/* ========================= */
/* Macro to define RPCs */
/* ========================= */

#define DEFINE_RPC(FUNC_NAME, CPP_METHOD, C_REQ, CPP_REQ, CPP_RESP, C_RESP,    \
                   C_DESC)                                                     \
  int FUNC_NAME(instrument_server_client_t *client, const C_REQ *req,          \
                C_RESP **resp_out) {                                           \
    if (!client || !req || !resp_out)                                          \
      return -1;                                                               \
                                                                               \
    CPP_REQ req_cpp;                                                           \
    if (!proto_c_to_cpp(&req->base, req_cpp))                                  \
      return -2;                                                               \
                                                                               \
    CPP_RESP resp_cpp;                                                         \
    grpc::ClientContext ctx;                                                   \
                                                                               \
    auto status = client->stub->CPP_METHOD(&ctx, req_cpp, &resp_cpp);          \
    if (!status.ok())                                                          \
      return -3;                                                               \
                                                                               \
    ProtobufCMessage *msg = proto_cpp_to_c(resp_cpp, &C_DESC);                 \
                                                                               \
    if (!msg)                                                                  \
      return -4;                                                               \
                                                                               \
    *resp_out = (C_RESP *)msg;                                                 \
    return 0;                                                                  \
  }

/* ========================= */
/* RPC Implementations */
/* ========================= */

DEFINE_RPC(instrument_server_client_daemon_status, DaemonStatus,
           Instserver__Server__V1__DaemonStatusRequest, v1::DaemonStatusRequest,
           v1::DaemonStatusResponse,
           Instserver__Server__V1__DaemonStatusResponse,
           instserver__server__v1__daemon_status_response__descriptor)

DEFINE_RPC(instrument_server_client_start_instrument, StartInstrument,
           Instserver__Server__V1__StartInstrumentRequest,
           v1::StartInstrumentRequest, v1::StartInstrumentResponse,
           Instserver__Server__V1__StartInstrumentResponse,
           instserver__server__v1__start_instrument_response__descriptor)

DEFINE_RPC(instrument_server_client_stop_instrument, StopInstrument,
           Instserver__Server__V1__StopInstrumentRequest,
           v1::StopInstrumentRequest, v1::StopInstrumentResponse,
           Instserver__Server__V1__StopInstrumentResponse,
           instserver__server__v1__stop_instrument_response__descriptor)

DEFINE_RPC(instrument_server_client_instrument_status, InstrumentStatus,
           Instserver__Server__V1__InstrumentStatusRequest,
           v1::InstrumentStatusRequest, v1::InstrumentStatusResponse,
           Instserver__Server__V1__InstrumentStatusResponse,
           instserver__server__v1__instrument_status_response__descriptor)

DEFINE_RPC(instrument_server_client_list_instruments, ListInstruments,
           Instserver__Server__V1__ListInstrumentsRequest,
           v1::ListInstrumentsRequest, v1::ListInstrumentsResponse,
           Instserver__Server__V1__ListInstrumentsResponse,
           instserver__server__v1__list_instruments_response__descriptor)

DEFINE_RPC(instrument_server_client_measure_job, MeasureJob,
           Instserver__Server__V1__MeasureJobRequest, v1::MeasureJobRequest,
           v1::MeasureJobResponse, Instserver__Server__V1__MeasureJobResponse,
           instserver__server__v1__measure_job_response__descriptor)

DEFINE_RPC(instrument_server_client_job_status, JobStatus,
           Instserver__Server__V1__JobStatusRequest, v1::JobStatusRequest,
           v1::JobStatusResponse, Instserver__Server__V1__JobStatusResponse,
           instserver__server__v1__job_status_response__descriptor)

DEFINE_RPC(instrument_server_client_measure_job_result, MeasureJobResult,
           Instserver__Server__V1__MeasureJobResultRequest,
           v1::MeasureJobResultRequest, v1::MeasureJobResultResponse,
           Instserver__Server__V1__MeasureJobResultResponse,
           instserver__server__v1__measure_job_result_response__descriptor)

DEFINE_RPC(instrument_server_client_job_list, JobList,
           Instserver__Server__V1__JobListRequest, v1::JobListRequest,
           v1::JobListResponse, Instserver__Server__V1__JobListResponse,
           instserver__server__v1__job_list_response__descriptor)

DEFINE_RPC(instrument_server_client_cancel_job, CancelJob,
           Instserver__Server__V1__CancelJobRequest, v1::CancelJobRequest,
           v1::CancelJobResponse, Instserver__Server__V1__CancelJobResponse,
           instserver__server__v1__cancel_job_response__descriptor)

DEFINE_RPC(instrument_server_client_discover, Discover,
           Instserver__Server__V1__DiscoverRequest, v1::DiscoverRequest,
           v1::DiscoverResponse, Instserver__Server__V1__DiscoverResponse,
           instserver__server__v1__discover_response__descriptor)

DEFINE_RPC(instrument_server_client_list_data_buffers, ListDataBuffers,
           Instserver__Server__V1__ListDataBuffersRequest,
           v1::ListDataBuffersRequest, v1::ListDataBuffersResponse,
           Instserver__Server__V1__ListDataBuffersResponse,
           instserver__server__v1__list_data_buffers_response__descriptor)

DEFINE_RPC(instrument_server_client_release_buffer, ReleaseBuffer,
           Instserver__Server__V1__ReleaseBufferRequest,
           v1::ReleaseBufferRequest, v1::ReleaseBufferResponse,
           Instserver__Server__V1__ReleaseBufferResponse,
           instserver__server__v1__release_buffer_response__descriptor)

DEFINE_RPC(instrument_server_client_get_buffer_metadata, GetBufferMetadata,
           Instserver__Server__V1__GetBufferMetadataRequest,
           v1::GetBufferMetadataRequest, v1::GetBufferMetadataResponse,
           Instserver__Server__V1__GetBufferMetadataResponse,
           instserver__server__v1__get_buffer_metadata_response__descriptor)

DEFINE_RPC(instrument_server_client_stop_daemon, StopDaemon,
           Instserver__Server__V1__DaemonStop, v1::DaemonStop,
           v1::StandardResponse, Instserver__Server__V1__StandardResponse,
           instserver__server__v1__standard_response__descriptor)

} // extern "C"
