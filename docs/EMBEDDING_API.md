# Embedding API

Guide to embedding the Instrument Script Server directly into other applications.

## Overview

The Embedding API allows you to run the Instrument Script Server inside your own process without launching a separate daemon. This is useful for:

- Higher-level orchestration servers that need direct instrument control
- Custom applications requiring programmatic instrument access
- Integration with existing C++ or Lua-based frameworks
- Scenarios requiring tighter lifecycle management

The embedded server maintains the same worker-process model, IPC mechanism, and Lua runtime as the standalone daemon.

## Key Concepts

**In-Process Server**: The `ServerDaemon` runs inside your application process and listens on localhost via HTTP RPC.

**Non-Blocking**:  Job submission returns immediately.  Query job status via API or provide callbacks.

**Process Isolation**:  Instruments still run in separate worker processes for fault tolerance.

## C++ API

### Starting the Server

```cpp
#include <instrument-server/server/ServerDaemon.hpp>

using namespace instserver;

// Get singleton instance
auto& daemon = ServerDaemon::instance();

// Optional: set RPC port (default:  8555)
daemon.set_rpc_port(9000);

// Start the daemon
if (! daemon.start()) {
    // Handle error
    return false;
}

// Server is now running and accepting commands
```

### Registering Instruments

Use the RPC interface or command handlers directly:

```cpp
#include <instrument-server/server/CommandHandlers.hpp>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// Prepare start parameters
json params;
params["config_path"] = "/path/to/instrument_config.yaml";
params["log_level"] = "info";

json response;
int rc = instserver::server::handle_start(params, response);

if (rc == 0 && response. value("ok", false)) {
    std::string name = response.value("name", "");
    // Instrument started successfully
}
```

### Submitting Measurement Jobs

```cpp
// Submit a measurement script
json measure_params;
measure_params["script_path"] = "/path/to/script.lua";

json measure_response;
int rc = instserver::server::handle_submit_measure(measure_params, measure_response);

if (rc == 0 && measure_response. value("ok", false)) {
    std::string job_id = measure_response.value("job_id", "");
    // Job queued, poll for completion
}
```

### Querying Job Status

```cpp
// Check job status
json status_params;
status_params["job_id"] = job_id;

json status_response;
int rc = instserver::server::handle_job_status(status_params, status_response);

if (rc == 0 && status_response.value("ok", false)) {
    std::string status = status_response. value("status", "");
    // Status:  "queued", "running", "completed", "failed", "canceled"
}
```

### Fetching Results

```cpp
// Retrieve completed job results
json result_params;
result_params["job_id"] = job_id;

json result_response;
int rc = instserver::server:: handle_job_result(result_params, result_response);

if (rc == 0 && result_response.value("ok", false)) {
    json result_data = result_response["result"];
    // Process measurement results
}
```

### Stopping the Server

```cpp
// Stop daemon and clean up
daemon.stop();
```

## HTTP RPC Interface

When the embedded server starts, it binds an HTTP RPC endpoint on `localhost`. You can interact with it from any language. 

### Configuration

Set the RPC port before starting:

```cpp
daemon.set_rpc_port(8555);  // Default
```

Or via environment variable:

```bash
export INSTRUMENT_SCRIPT_SERVER_RPC_PORT=8555
```

### Available Commands

All commands use `POST /rpc` with JSON body:

```json
{
  "command": "<command_name>",
  "params": { /* command-specific params */ }
}
```

**Supported Commands:**

- `start` - Start an instrument
- `stop` - Stop an instrument
- `status` - Query instrument status
- `list` - List all instruments
- `submit_measure` - Submit a measurement job
- `job_status` - Query job status
- `job_result` - Fetch job result
- `job_list` - List all jobs
- `job_cancel` - Cancel a queued/running job

See [RPC.md](RPC.md) for detailed command reference.

## Python Example

```python
import requests
import json
import time

# Server address
RPC_URL = "http://127.0.0.1:8555/rpc"

def rpc_call(command, params):
    payload = {"command": command, "params": params}
    response = requests.post(RPC_URL, json=payload)
    return response.json()

# Start an instrument
result = rpc_call("start", {"config_path": "/path/to/config.yaml"})
if result. get("ok"):
    print(f"Started instrument: {result['name']}")

# Submit measurement job
result = rpc_call("submit_measure", {"script_path": "/path/to/script.lua"})
if result.get("ok"):
    job_id = result["job_id"]
    print(f"Job submitted: {job_id}")
    
    # Poll for completion
    while True:
        status = rpc_call("job_status", {"job_id": job_id})
        if status["status"] == "completed":
            break
        time.sleep(0.1)
    
    # Fetch results
    result = rpc_call("job_result", {"job_id": job_id})
    print(f"Results: {result['result']}")
```

## Environment Variables

- **`INSTRUMENT_SCRIPT_SERVER_RPC_PORT`**: Sets the RPC server port (default: `8555`). Must be set before calling `daemon.start()`.

## Job Queue Behavior

The embedded server uses the same job queue as the standalone daemon.  See [JOB_SCHEDULING. md](JOB_SCHEDULING.md) for details on: 

- Job lifecycle and states
- Queue ordering and prioritization
- Status/list command immediate execution

## Thread Safety

- `ServerDaemon:: instance()` is thread-safe
- `start()` and `stop()` should be called from the same thread
- Command handlers can be called from multiple threads
- The RPC endpoint handles concurrent connections

## Lifecycle Management

**Typical Pattern:**

1. Configure daemon (set RPC port if needed)
2. Call `daemon.start()`
3. Register instruments
4. Submit jobs and query status
5. Call `daemon.stop()` on shutdown

**Error Handling:**

- Check return codes from command handlers
- Verify `ok` field in JSON responses
- Monitor logs for detailed error messages

## Comparison:  Embedded vs.  Standalone

| Feature | Embedded | Standalone CLI |
|---------|----------|----------------|
| Daemon Process | In your process | Separate background process |
| Communication | Direct C++ API or HTTP RPC | CLI commands or HTTP RPC |
| Lifecycle | Managed by your app | Managed by system |
| Latency | Lower (no process spawn) | Higher (process per command) |
| Use Case | Tight integration | Scripting, automation |

## See Also

- [RPC.md](RPC.md) - HTTP RPC command reference
- [JOB_SCHEDULING.md](JOB_SCHEDULING.md) - Job queue and scheduling details
- [CLI_USAGE. md](CLI_USAGE.md) - Standalone CLI usage
- [ARCHITECTURE.md](ARCHITECTURE.md) - System design and components
