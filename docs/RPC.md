# HTTP RPC Interface

Complete reference for the Instrument Script Server HTTP RPC API.

## Overview

The HTTP RPC interface provides programmatic access to all server operations without spawning CLI processes. This is the recommended interface for:

- Embedding the server in other applications
- High-throughput automation
- Custom control systems
- Web-based interfaces

## Configuration

### Port Setup

**Default Port**: `8555`  
**Binding**: `127.0.0.1` (localhost only)

**Set via environment variable:**

```bash
export INSTRUMENT_SCRIPT_SERVER_RPC_PORT=8555
instrument-server daemon start
```

**Set programmatically (C++):**

```cpp
auto& daemon = instserver::ServerDaemon::instance();
daemon.set_rpc_port(8555);
daemon.start();
```

## Request Format

All requests use `POST /rpc` with JSON body:

```json
{
  "command": "<command_name>",
  "params": { /* command-specific parameters */ }
}
```

**Headers:**

- `Content-Type: application/json`

## Response Format

All responses are JSON:

```json
{
  "ok": true,
  /* command-specific response fields */
}
```

On error:

```json
{
  "ok": false,
  "error":  "Error message"
}
```

## Command Reference

### Daemon Management

#### `daemon` - Control daemon lifecycle

**Parameters:**

```json
{
  "action":  "start|stop|status",
  "log_level": "debug|info|warn|error",  // optional, for "start"
  "block":  true|false                     // optional, default true
}
```

**Response:**

```json
{
  "ok": true,
  "pid": 12345  // for "start" action
}
```

**Example:**

```bash
curl -X POST http://127.0.0.1:8555/rpc \
  -H "Content-Type: application/json" \
  -d '{"command":"daemon","params":{"action":"status"}}'
```

---

### Instrument Management

#### `start` - Start an instrument

**Parameters:**

```json
{
  "config_path": "/path/to/config.yaml",
  "plugin_path": "/path/to/plugin. so",  // optional
  "log_level": "info"                    // optional
}
```

**Response:**

```json
{
  "ok": true,
  "name": "InstrumentName"
}
```

#### `stop` - Stop an instrument

**Parameters:**

```json
{
  "name": "InstrumentName"
}
```

**Response:**

```json
{
  "ok": true
}
```

#### `status` - Query instrument status

**Parameters:**

```json
{
  "name": "InstrumentName"
}
```

**Response:**

```json
{
  "ok": true,
  "name": "InstrumentName",
  "alive": true,
  "stats": {
    "commands_sent": 150,
    "commands_completed": 148,
    "commands_failed": 0,
    "commands_timeout": 2
  }
}
```

**Note:** Status queries execute immediately without waiting for queued measure jobs.

#### `list` - List all instruments

**Parameters:**

```json
{}
```

**Response:**

```json
{
  "ok": true,
  "instruments": ["DMM1", "DAC1", "Scope1"]
}
```

**Note:** List queries execute immediately without waiting for queued measure jobs.

---

### Measurement Jobs

#### `submit_measure` - Submit measurement script

**Parameters:**

```json
{
  "script_path":  "/path/to/script. lua"
}
```

**Response:**

```json
{
  "ok": true,
  "job_id": "job_20260116_123456_a1b2c3"
}
```

**Notes:**

- Job is queued and executed asynchronously
- Script is parsed immediately to queue instrument commands
- Multiple measure jobs can have overlapping execution

#### `job_status` - Query job status

**Parameters:**

```json
{
  "job_id":  "job_20260116_123456_a1b2c3"
}
```

**Response:**

```json
{
  "ok": true,
  "job_id": "job_20260116_123456_a1b2c3",
  "status": "completed",
  "created_at":  1705401234567
}
```

**Status values:**

- `queued` - Waiting in queue
- `running` - Currently executing
- `completed` - Finished successfully
- `failed` - Encountered error
- `canceled` - Was canceled

#### `job_result` - Fetch job result

**Parameters:**

```json
{
  "job_id":  "job_20260116_123456_a1b2c3"
}
```

**Response:**

```json
{
  "ok": true,
  "job_id": "job_20260116_123456_a1b2c3",
  "result": {
    "status":  "success",
    "script":  "script.lua",
    "results": [
      {
        "index": 0,
        "instrument": "DMM1",
        "verb": "MEASURE",
        "params": {},
        "executed_at_ms":  1705401234789,
        "return": {
          "type": "double",
          "value": 3.14159
        }
      }
    ]
  }
}
```

**Note:** Only available after job completes.

#### `job_list` - List all jobs

**Parameters:**

```json
{}
```

**Response:**

```json
{
  "ok": true,
  "jobs": [
    {
      "job_id":  "job_20260116_123456_a1b2c3",
      "type": "measure",
      "status": "completed",
      "created_at":  1705401234567
    }
  ]
}
```

#### `job_cancel` - Cancel a job

**Parameters:**

```json
{
  "job_id": "job_20260116_123456_a1b2c3"
}
```

**Response:**

```json
{
  "ok": true,
  "message": "Job canceled"
}
```

**Notes:**

- Only queued or running jobs can be canceled
- Cancellation is cooperative
- Commands already sent to instruments cannot be interrupted

---

### Testing & Discovery

#### `test` - Test instrument command

**Parameters:**

```json
{
  "config_path": "/path/to/config.yaml",
  "verb": "CommandVerb",
  "params": {},                          // command parameters
  "plugin_path": "/path/to/plugin.so"  // optional
}
```

**Response:**

```json
{
  "ok":  true,
  "result": "command_return_value"
}
```

#### `discover` - Discover plugins

**Parameters:**

```json
{
  "paths": ["/path/to/plugins"]  // optional
}
```

**Response:**

```json
{
  "ok": true,
  "plugins": [
    {
      "protocol":  "VISA",
      "path": "/usr/local/lib/instrument-plugins/visa_builtin.so",
      "name":  "VISA Plugin",
      "version": "1.0.0"
    }
  ]
}
```

#### `plugins` - List available plugins

**Parameters:**

```json
{}
```

**Response:**

```json
{
  "ok": true,
  "plugins": {
    "VISA":  "/usr/local/lib/instrument-plugins/visa_builtin. so",
    "SimpleSerial": "/usr/local/lib/instrument-plugins/simple_serial_plugin.so"
  }
}
```

---

## Complete Example (curl)

```bash
# Start daemon
curl -X POST http://127.0.0.1:8555/rpc \
  -H "Content-Type: application/json" \
  -d '{"command":"daemon","params":{"action":"start"}}'

# Start an instrument
curl -X POST http://127.0.0.1:8555/rpc \
  -H "Content-Type: application/json" \
  -d '{"command":"start","params": {"config_path":"configs/dmm1.yaml"}}'

# Submit measurement
curl -X POST http://127.0.0.1:8555/rpc \
  -H "Content-Type:  application/json" \
  -d '{"command":"submit_measure","params":{"script_path":"scripts/measure.lua"}}'

# Response:  {"ok":true,"job_id":"job_20260116_123456_a1b2c3"}

# Poll job status
curl -X POST http://127.0.0.1:8555/rpc \
  -H "Content-Type: application/json" \
  -d '{"command":"job_status","params":{"job_id":"job_20260116_123456_a1b2c3"}}'

# Fetch results
curl -X POST http://127.0.0.1:8555/rpc \
  -H "Content-Type:  application/json" \
  -d '{"command":"job_result","params":{"job_id":"job_20260116_123456_a1b2c3"}}'
```

## Python Client Example

```python
import requests
import time

class InstrumentServerRPC:
    def __init__(self, host="127.0.0.1", port=8555):
        self.url = f"http://{host}:{port}/rpc"
    
    def call(self, command, params=None):
        payload = {"command": command, "params": params or {}}
        response = requests. post(self.url, json=payload)
        return response.json()
    
    def submit_measure(self, script_path):
        return self.call("submit_measure", {"script_path":  script_path})
    
    def job_status(self, job_id):
        return self. call("job_status", {"job_id":  job_id})
    
    def wait_for_job(self, job_id, poll_interval=0.1):
        while True:
            status = self. job_status(job_id)
            if status["status"] in ["completed", "failed", "canceled"]:
                return status
            time.sleep(poll_interval)
    
    def job_result(self, job_id):
        return self.call("job_result", {"job_id": job_id})

# Usage
client = InstrumentServerRPC()

# Submit measurement
response = client.submit_measure("scripts/measure.lua")
job_id = response["job_id"]
print(f"Job submitted: {job_id}")

# Wait for completion
client.wait_for_job(job_id)

# Get results
result = client.job_result(job_id)
print(f"Results: {result}")
```

## Security Notes

- RPC server binds to `127.0.0.1` only (localhost)
- No authentication by default
- Designed for local control and automation
- **Do not expose to untrusted networks**
- Consider firewall rules if running on shared systems

## Performance

- **Request Latency**: ~200 µs per RPC call
- **Job Submission**: Non-blocking, returns immediately
- **Status Queries**:  Fast-tracked, no queue blocking
- **Throughput**: Thousands of requests per second

## See Also

- [EMBEDDING_API](EMBEDDING_API.md) - Embedding the server in C++
- [JOB_SCHEDULING](JOB_SCHEDULING.md) - Job queue behavior
- [CLI](CLI_USAGE.md) - Command-line interface
- [ARCHITECTURE](ARCHITECTURE.md) - System architecture
