#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#include "instserver/server/v1/daemon_messages.pb-c.h"
#include <stdint.h>

/**
 * @file instrument-script-server-client.h
 * @brief C99 client API for interacting with the Instrument gRPC daemon.
 *
 * This API provides a pure-C interface to a gRPC server by internally:
 *
 *  - Serializing protobuf-c request structs into binary
 *  - Converting them into C++ protobuf objects
 *  - Sending them via gRPC
 *  - Receiving responses and converting them back into protobuf-c structs
 *
 * ---
 *
 * ## Memory Ownership Rules (IMPORTANT)
 *
 * For every function of the form:
 *
 *     int instrument_server_client_*(..., ResponseType** resp_out)
 *
 * - The response is **allocated internally**
 * - The caller **takes ownership**
 * - The caller **must free it** using:
 *
 *     instrument_server_client_free_response(resp);
 *
 *
 * ## Request Ownership
 *
 * - All request objects (`req`) are **owned by the caller**
 * - They are **NOT modified or freed** by the library
 *
 *
 * ## Return Value Convention
 *
 * - `0` → success (RPC completed, response is valid)
 * - non-zero → failure (network error, decode error, etc.)
 *
 *
 * ## Example Usage
 *
 * @code
 * instrument_server_client_t* client = instrument_server_client_create(50051);
 *
 * Instserver__Server__V1__JobListRequest req =
 *     INSTSERVER__SERVER__V1__JOB_LIST_REQUEST__INIT;
 *
 * Instserver__Server__V1__JobListResponse* resp = NULL;
 *
 * if (instrument_server_client_job_list(client, &req, &resp) == 0) {
 *     for (size_t i = 0; i < resp->n_jobs; i++) {
 *         printf("Job ID: %u\n", resp->jobs[i]->key);
 *     }
 *
 *     instrument_server_client_free_response(resp);
 * }
 *
 * instrument_server_client_destroy(client);
 * @endcode
 */

/* ========================= */
/* Opaque Client Type */
/* ========================= */

/**
 * @brief Opaque client handle.
 *
 * Internally wraps a C++ gRPC stub and communication channel.
 */
typedef struct instrument_server_client instrument_server_client_t;

/* ========================= */
/* Lifecycle */
/* ========================= */

/**
 * @brief Create a new client connected to localhost at the given port.
 *
 * @param port gRPC server port (e.g. 50051)
 * @return Pointer to client instance, or NULL on failure
 */
instrument_server_client_t *instrument_server_client_create(uint16_t port);

/**
 * @brief Destroy a client and release all associated resources.
 *
 * @param client Client instance (may be NULL)
 */
void instrument_server_client_destroy(instrument_server_client_t *client);

/**
 * @brief Check whether a daemon is currently reachable and running.
 *
 * Attempts a gRPC DaemonStatus call on the given port. Returns 1 if the
 * daemon responds and reports itself running, 0 otherwise. This is a
 * lightweight liveness check that does not require any server-side headers.
 *
 * @param port gRPC server port (e.g. 8555)
 * @return 1 if daemon is running, 0 if not reachable or not running
 */
int instrument_server_client_is_daemon_running(uint16_t port);

/* ========================= */
/* Generic free */
/* ========================= */

/**
 * @brief Free a response message returned by any API call.
 *
 * This must be called for every `resp_out` returned by the API.
 *
 * Internally calls:
 *   protobuf_c_message_free_unpacked(...)
 *
 * @param msg Response pointer returned from API (safe to pass NULL)
 */
void instrument_server_client_free_response(void *msg);

/* ========================= */
/* RPC Wrappers */
/* ========================= */

/**
 * @brief Query daemon status.
 */
int instrument_server_client_daemon_status(
    instrument_server_client_t *client,
    const Instserver__Server__V1__DaemonStatusRequest *req,
    Instserver__Server__V1__DaemonStatusResponse **resp_out);

/**
 * @brief Start an instrument.
 */
int instrument_server_client_start_instrument(
    instrument_server_client_t *client,
    const Instserver__Server__V1__StartInstrumentRequest *req,
    Instserver__Server__V1__StartInstrumentResponse **resp_out);

/**
 * @brief Stop an instrument.
 */
int instrument_server_client_stop_instrument(
    instrument_server_client_t *client,
    const Instserver__Server__V1__StopInstrumentRequest *req,
    Instserver__Server__V1__StopInstrumentResponse **resp_out);

/**
 * @brief Get status of a specific instrument.
 */
int instrument_server_client_instrument_status(
    instrument_server_client_t *client,
    const Instserver__Server__V1__InstrumentStatusRequest *req,
    Instserver__Server__V1__InstrumentStatusResponse **resp_out);

/**
 * @brief List all available instruments.
 */
int instrument_server_client_list_instruments(
    instrument_server_client_t *client,
    const Instserver__Server__V1__ListInstrumentsRequest *req,
    Instserver__Server__V1__ListInstrumentsResponse **resp_out);

/**
 * @brief Submit a new measurement job.
 */
int instrument_server_client_measure_job(
    instrument_server_client_t *client,
    const Instserver__Server__V1__MeasureJobRequest *req,
    Instserver__Server__V1__MeasureJobResponse **resp_out);

/**
 * @brief Query status of a job.
 */
int instrument_server_client_job_status(
    instrument_server_client_t *client,
    const Instserver__Server__V1__JobStatusRequest *req,
    Instserver__Server__V1__JobStatusResponse **resp_out);

/**
 * @brief Retrieve results of a completed job.
 */
int instrument_server_client_measure_job_result(
    instrument_server_client_t *client,
    const Instserver__Server__V1__MeasureJobResultRequest *req,
    Instserver__Server__V1__MeasureJobResultResponse **resp_out);

/**
 * @brief List all jobs.
 */
int instrument_server_client_job_list(
    instrument_server_client_t *client,
    const Instserver__Server__V1__JobListRequest *req,
    Instserver__Server__V1__JobListResponse **resp_out);

/**
 * @brief Cancel a running job.
 */
int instrument_server_client_cancel_job(
    instrument_server_client_t *client,
    const Instserver__Server__V1__CancelJobRequest *req,
    Instserver__Server__V1__CancelJobResponse **resp_out);

/**
 * @brief Discover available plugins/instruments.
 */
int instrument_server_client_discover(
    instrument_server_client_t *client,
    const Instserver__Server__V1__DiscoverRequest *req,
    Instserver__Server__V1__DiscoverResponse **resp_out);

/**
 * @brief List all data buffers currently available.
 */
int instrument_server_client_list_data_buffers(
    instrument_server_client_t *client,
    const Instserver__Server__V1__ListDataBuffersRequest *req,
    Instserver__Server__V1__ListDataBuffersResponse **resp_out);

/**
 * @brief Release a data buffer by ID.
 */
int instrument_server_client_release_buffer(
    instrument_server_client_t *client,
    const Instserver__Server__V1__ReleaseBufferRequest *req,
    Instserver__Server__V1__ReleaseBufferResponse **resp_out);

/**
 * @brief Get metadata for a specific buffer.
 */
int instrument_server_client_get_buffer_metadata(
    instrument_server_client_t *client,
    const Instserver__Server__V1__GetBufferMetadataRequest *req,
    Instserver__Server__V1__GetBufferMetadataResponse **resp_out);

/**
 * @brief Stop the daemon.
 */
int instrument_server_client_stop_daemon(
    instrument_server_client_t *client,
    const Instserver__Server__V1__DaemonStop *req,
    Instserver__Server__V1__StandardResponse **resp_out);

#ifdef __cplusplus
}
#endif
