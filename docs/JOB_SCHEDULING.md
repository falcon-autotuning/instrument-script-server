# Job Scheduling & Queue Management

Guide to the job queue system and request handling in Instrument Script Server.

## Overview

The server uses a job-based system to handle measurement requests and other commands. Jobs are queued and executed in order with special handling for different command types.

**Key Features:**

- Ordered queue processing for measurement jobs
- Immediate execution for status/list queries
- Script parsing and staging before execution
- Non-blocking job submission via RPC

## Job Queue Architecture

The `JobManager` maintains a FIFO queue for all jobs.  Jobs are processed by a dedicated worker thread.

### Job Types

- **`measure`**: Execute a Lua measurement script
- **`sleep`**: Testing/timing placeholder (sleeps for specified duration)
- **`status`**: Query instrument status (fast-tracked)
- **`list`**: List instruments (fast-tracked)

### Job States

| State | Description |
|-------|-------------|
| `queued` | Job submitted, waiting in queue |
| `running` | Job currently executing |
| `completed` | Job finished successfully |
| `failed` | Job encountered an error |
| `canceling` | Cancellation requested |
| `canceled` | Job was canceled |

## Queue Behavior

### Measure Jobs

**Enqueue-and-Parse Pattern:**

When a `measure` job is dequeued: 

1. Create Lua state with `RuntimeContext` in enqueue mode
2. Parse the script to extract and queue all instrument commands
3. Commands are sent to instrument workers immediately
4. Worker thread moves to next job while instruments execute

This allows: 
- Multiple measure jobs to have commands in flight simultaneously
- Faster job throughput
- Instrument-level parallelism

**Concurrency:**

- Multiple measure jobs can have active commands simultaneously
- Non-measure jobs wait for all active measure jobs to complete
- Prevents interference with status/list queries

### Status and List Commands

**Fast-Track Execution:**

Status and list commands bypass the normal queue delay:

1. Job is added to queue normally
2. Worker thread checks job type on dequeue
3. If `status` or `list`, executes immediately without waiting
4. No blocking on active measure jobs

This ensures:
- Real-time monitoring during long measurements
- Responsive status queries
- No queue head-of-line blocking

## Submitting Jobs

### Via RPC (Recommended for Embedding)

```bash
curl -X POST http://127.0.0.1:8555/rpc \
  -H "Content-Type: application/json" \
  -d '{
    "command": "submit_measure",
    "params": {
      "script_path": "/path/to/script.lua"
    }
  }'
```

Response: 

```json
{
  "ok": true,
  "job_id": "job_20260116_123456_a1b2c3"
}
```

### Via C++ API

```cpp
#include <instrument-server/server/JobManager.hpp>

using namespace instserver::server;

std::string job_id = JobManager::instance().submit_measure(
    "/path/to/script.lua",
    nlohmann::json::object()
);
```

### Via CLI (Background)

The CLI `measure` command blocks by default. For non-blocking: 

```bash
instrument-server measure script.lua &
JOB_PID=$! 

# Query via RPC for status
# (CLI doesn't expose job_id directly)
```

## Querying Job Status

### Via RPC

```bash
curl -X POST http://127.0.0.1:8555/rpc \
  -H "Content-Type: application/json" \
  -d '{
    "command": "job_status",
    "params": {
      "job_id": "job_20260116_123456_a1b2c3"
    }
  }'
```

Response:

```json
{
  "ok": true,
  "job_id": "job_20260116_123456_a1b2c3",
  "status": "running",
  "created_at": 1705401234567
}
```

### Via C++ API

```cpp
JobInfo info;
if (JobManager::instance().get_job_info(job_id, info)) {
    std::cout << "Status: " << info.status << std::endl;
}
```

## Fetching Results

### Via RPC

```bash
curl -X POST http://127.0.0.1:8555/rpc \
  -H "Content-Type: application/json" \
  -d '{
    "command": "job_result",
    "params": {
      "job_id": "job_20260116_123456_a1b2c3"
    }
  }'
```

Response (for completed measure job):

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
        "executed_at_ms": 1705401234789,
        "return":  {
          "type": "double",
          "value": 3.14159
        }
      }
    ]
  }
}
```

### Via C++ API

```cpp
nlohmann::json result;
if (JobManager::instance().get_job_result(job_id, result)) {
    // Process result JSON
}
```

## Listing All Jobs

### Via RPC

```bash
curl -X POST http://127.0.0.1:8555/rpc \
  -H "Content-Type: application/json" \
  -d '{
    "command": "job_list",
    "params": {}
  }'
```

Response:

```json
{
  "ok": true,
  "jobs":  [
    {
      "job_id": "job_20260116_123456_a1b2c3",
      "type": "measure",
      "status": "completed",
      "created_at":  1705401234567
    },
    {
      "job_id": "job_20260116_123457_d4e5f6",
      "type": "measure",
      "status": "running",
      "created_at":  1705401235123
    }
  ]
}
```

## Canceling Jobs

### Via RPC

```bash
curl -X POST http://127.0.0.1:8555/rpc \
  -H "Content-Type: application/json" \
  -d '{
    "command": "job_cancel",
    "params": {
      "job_id": "job_20260116_123456_a1b2c3"
    }
  }'
```

**Notes:**

- Only `queued` and `running` jobs can be canceled
- Cancellation is cooperative: running measure jobs check periodically
- Commands already sent to instruments cannot be interrupted

## Command Staging

### What is Staging?

When a measure job runs: 

1. **Parse Phase**: Lua script is executed in "enqueue mode"
   - `context: call()` sends commands to instrument queues
   - No waiting for command completion
   - Script executes quickly

2. **Execution Phase**:  Instruments process queued commands
   - Commands execute in order per instrument
   - Parallel execution across instruments
   - Results collected automatically

### Benefits

- **Throughput**: Multiple measure jobs can queue commands without blocking
- **Parallelism**: Instruments process commands concurrently
- **Responsiveness**: Job submission returns immediately

### Limitations

- Script logic cannot branch on command results during parse phase
- All `context:call()` commands are queued upfront
- Use `context:parallel()` for explicit synchronization points

## Practical Examples

### High-Throughput Measurement Queue

```python
import requests

RPC_URL = "http://127.0.0.1:8555/rpc"

def submit_job(script):
    r = requests.post(RPC_URL, json={
        "command": "submit_measure",
        "params": {"script_path": script}
    })
    return r.json()["job_id"]

# Submit batch of measurements
job_ids = []
for i in range(10):
    job_id = submit_job(f"scripts/measure_{i}.lua")
    job_ids.append(job_id)
    print(f"Submitted {job_id}")

# Jobs are queued and execute in order
# Can query status in parallel without blocking
```

### Real-Time Status Monitoring

```python
import time

def get_status(instrument_name):
    r = requests.post(RPC_URL, json={
        "command": "status",
        "params": {"name": instrument_name}
    })
    return r.json()

# Submit long-running measurement
job_id = submit_job("long_measurement.lua")

# Monitor instrument status during measurement
while True:
    status = get_status("DMM1")
    print(f"DMM1 commands: {status['stats']['commands_sent']}")
    
    # Check if job done
    job_status = requests.post(RPC_URL, json={
        "command": "job_status",
        "params": {"job_id": job_id}
    }).json()
    
    if job_status["status"] in ["completed", "failed"]: 
        break
    
    time. sleep(1.0)
```

## Queue Ordering Summary

| Job Type | Queue Position | Waits for Measure Jobs | Executed When |
|----------|----------------|------------------------|---------------|
| `measure` | FIFO | No | Immediately on dequeue |
| `status` | FIFO | No | Immediately on dequeue |
| `list` | FIFO | No | Immediately on dequeue |
| Other | FIFO | Yes | After all measure jobs complete |

## Performance Characteristics

- **Job Submission**: ~100 µs (immediate return)
- **Queue Overhead**: ~50 µs per job dispatch
- **Status Query**: ~200 µs (no queue blocking)
- **Multiple Measure Jobs**: Commands overlapped at instrument level

## See Also

- [EMBEDDING_API.md](EMBEDDING_API.md) - Embedding the server in your app
- [RPC.md](RPC.md) - HTTP RPC command reference
- [CLI_USAGE.md](CLI_USAGE.md) - Command-line usage
- [ARCHITECTURE.md](ARCHITECTURE.md) - System design overview
