#pragma once

#include "instserver/daemon/v1/daemon_messages.pb.h"
#include <sol/sol.hpp>

namespace instserver::daemon {

sol::object variable_to_lua(sol::state_view, const v1::VariableValue *);

void load_optional_lua_libs(sol::state &);

using namespace instserver::daemon::v1;

int handle_daemon_status(const DaemonStatusRequest &req,
                         DaemonStatusResponse *resp);

int handle_start_instrument(const StartInstrumentRequest &req,
                            StartInstrumentResponse *resp);

int handle_stop_instrument(const StopInstrumentRequest &req,
                           StopInstrumentResponse *resp);

int handle_instrument_status(const InstrumentStatusRequest &req,
                             InstrumentStatusResponse *resp);

int handle_list_instruments(const ListInstrumentsRequest &req,
                            ListInstrumentsResponse *resp);

int handle_measure(const MeasureJobRequest &req,
                   MeasureJobResultResponse *resp);

int handle_measure_job(const MeasureJobRequest &req, MeasureJobResponse *resp);

int handle_job_status(const JobStatusRequest &req, JobStatusResponse *resp);

int handle_measure_job_result(const MeasureJobResultRequest &req,
                              MeasureJobResultResponse *resp);

int handle_job_list(const JobListRequest &req, JobListResponse *resp);

int handle_cancel_job(const CancelJobRequest &req, CancelJobResponse *resp);

int handle_discover(const DiscoverRequest &req, DiscoverResponse *resp);

int handle_list_buffers(const ListDataBuffersRequest &req,
                        ListDataBuffersResponse *resp);

int handle_release_buffer(const ReleaseBufferRequest &req,
                          ReleaseBufferResponse *resp);

int handle_get_buffer_metadata(const GetBufferMetadataRequest &req,
                               GetBufferMetadataResponse *resp);

// Special case (no response message)
int handle_daemon_stop(const DaemonStop &req,
                       void *unused /* or remove later */);

} // namespace instserver::daemon
