#pragma once

#include "instrument-script-server/export.h"
#include "instserver/daemon/v1/daemon_messages.pb.h"
#include <sol/sol.hpp>

namespace instserver::daemon {

INSTRUMENT_SERVER_API sol::object variable_to_lua(sol::state_view,
                                                  const v1::VariableValue *);

INSTRUMENT_SERVER_API void load_optional_lua_libs(sol::state &);

using namespace instserver::daemon::v1;

INSTRUMENT_SERVER_API int handle_daemon_status(const DaemonStatusRequest &req,
                                               DaemonStatusResponse *resp);

INSTRUMENT_SERVER_API int
handle_start_instrument(const StartInstrumentRequest &req,
                        StartInstrumentResponse *resp);

INSTRUMENT_SERVER_API int
handle_stop_instrument(const StopInstrumentRequest &req,
                       StopInstrumentResponse *resp);

INSTRUMENT_SERVER_API int
handle_instrument_status(const InstrumentStatusRequest &req,
                         InstrumentStatusResponse *resp);

INSTRUMENT_SERVER_API int
handle_list_instruments(const ListInstrumentsRequest &req,
                        ListInstrumentsResponse *resp);

INSTRUMENT_SERVER_API int handle_measure(const MeasureJobRequest &req,
                                         MeasureJobResultResponse *resp);

INSTRUMENT_SERVER_API int handle_measure_job(const MeasureJobRequest &req,
                                             MeasureJobResponse *resp);

INSTRUMENT_SERVER_API int handle_job_status(const JobStatusRequest &req,
                                            JobStatusResponse *resp);

INSTRUMENT_SERVER_API int
handle_measure_job_result(const MeasureJobResultRequest &req,
                          MeasureJobResultResponse *resp);

INSTRUMENT_SERVER_API int handle_job_list(const JobListRequest &req,
                                          JobListResponse *resp);

INSTRUMENT_SERVER_API int handle_cancel_job(const CancelJobRequest &req,
                                            CancelJobResponse *resp);

INSTRUMENT_SERVER_API int handle_discover(const DiscoverRequest &req,
                                          DiscoverResponse *resp);

INSTRUMENT_SERVER_API int handle_list_buffers(const ListDataBuffersRequest &req,
                                              ListDataBuffersResponse *resp);

INSTRUMENT_SERVER_API int handle_release_buffer(const ReleaseBufferRequest &req,
                                                ReleaseBufferResponse *resp);

INSTRUMENT_SERVER_API int
handle_get_buffer_metadata(const GetBufferMetadataRequest &req,
                           GetBufferMetadataResponse *resp);

// Special case (no response message)
INSTRUMENT_SERVER_API int
handle_daemon_stop(const DaemonStop &req, void *unused /* or remove later */);

} // namespace instserver::daemon
