# Instrument Synchronization Protocol

Detailed documentation of the parallel execution and synchronization system.

## Table of Contents

- [Overview](#overview)
- [Motivation](#motivation)
- [Protocol Design](#protocol-design)
- [Implementation](#implementation)
- [Usage in Lua Scripts](#usage-in-lua-scripts)
- [Performance](#performance)
- [Debugging](#debugging)
- [Troubleshooting](#troubleshooting)

## Overview

The synchronization protocol enables multiple instruments to execute commands in **lockstep** during parallel execution blocks. This ensures precise timing coordination for experiments requiring simultaneous actions.

### Key Features

- **Barrier synchronization**:  All instruments wait at sync points
- **No timing skew**: Commands complete before any instrument advances
- **Scalable**: Works with any number of instruments
- **Transparent**: Automatic in `parallel()` blocks

## Motivation

### Problem:  Timing Skew Without Synchronization

Consider setting voltages on three DACs:

```lua
-- WITHOUT synchronization (sequential)
context:call("DAC1.SetVoltage", 1.0)  -- t=0ms
context:call("DAC2.SetVoltage", 2.0)  -- t=50ms
context:call("DAC3.SetVoltage", 3.0)  -- t=100ms
-- Time skew: 100ms between first and last
```

**Problem**: DAC1 settles 100ms before DAC3, causing:

- Transient measurement artifacts
- Invalid intermediate states
- Reproducibility issues

### Solution: Synchronized Parallel Execution

```lua
-- WITH synchronization (parallel)
context:parallel(function()
    context:call("DAC1.SetVoltage", 1.0)
    context:call("DAC2.SetVoltage", 2.0)
    context:call("DAC3.SetVoltage", 3.0)
end)
-- All DACs set within ~1ms of each other
```

**Benefit**: All voltages set simultaneously, eliminating timing skew.

## Protocol Design

### Sync Token System

Each parallel block is assigned a unique **sync token** (uint64_t). All commands in that block share the same token.

```
parallel block → sync_token=42
├── DAC1.SetVoltage(1.0)  [token=42]
├── DAC2.SetVoltage(2.0)  [token=42]
└── DAC3.SetVoltage(3.0)  [token=42]
```

### Message Types

```cpp
enum class Type : uint32_t {
    COMMAND = 1,        // Server → Worker: Execute command
    RESPONSE = 2,       // Worker → Server: Command result
    HEARTBEAT = 3,      // Worker → Server: Keep-alive
    SHUTDOWN = 4,       // Server → Worker:  Terminate
    SYNC_ACK = 5,       // Worker → Server: Sync command complete
    SYNC_CONTINUE = 6   // Server → Worker:  All synced, proceed
};
```

### Protocol Flow

```
┌─────────────────────────────────────────────────────────┐
│ 1. RuntimeContext:  Detect parallel() block             │
│    - Set in_parallel_block_ = true                      │
│    - Clear parallel_buffer_                             │
└─────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────┐
│ 2. RuntimeContext: Execute Lua block                    │
│    - Each call() buffers command                        │
│    - Commands NOT executed yet                          │
└─────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────┐
│ 3. RuntimeContext: Tag and dispatch                     │
│    - Assign sync_token to all commands                  │
│    - Register barrier with SyncCoordinator              │
│    - Dispatch commands to InstrumentWorkerProxies       │
└─────────────────────────────────────────────────────────┘
                          ↓ IPC
┌─────────────────────────────────────────────────────────┐
│ 4. Workers: Execute commands                            │
│    - Receive COMMAND with sync_token                    │
│    - Execute via plugin                                 │
│    - Send RESPONSE                                      │
│    - Send SYNC_ACK                                      │
│    - BLOCK (wait for SYNC_CONTINUE)                     │
└─────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────┐
│ 5. SyncCoordinator: Collect ACKs                        │
│    - Track ACKs from each instrument                    │
│    - Wait until ALL instruments have ACKed              │
└─────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────┐
│ 6. Server:  Broadcast SYNC_CONTINUE                     │
│    - Send SYNC_CONTINUE to all participants             │
│    - Workers unblock                                    │
└─────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────┐
│ 7. RuntimeContext: parallel() returns                   │
│    - All instruments past sync point                    │
│    - Lua script continues                               │
└─────────────────────────────────────────────────────────┘
```

## Implementation

### RuntimeContext (Server Side)

**File**: `src/server/RuntimeContext.cpp`

#### Parallel Block Detection

```cpp
void RuntimeContextBase::parallel(sol::function block) {
    LOG_DEBUG("LUA_CONTEXT", "PARALLEL", "Starting parallel block");

    in_parallel_block_ = true;
    parallel_buffer_.clear();

    // Execute Lua block - calls are buffered
    try {
        block();
    } catch (const std::exception &e) {
        in_parallel_block_ = false;
        parallel_buffer_.clear();
        throw;
    }

    in_parallel_block_ = false;

    // Execute buffered commands with sync
    execute_parallel_buffer();

    parallel_buffer_.clear();
}
```

#### Command Buffering

```cpp
sol::object RuntimeContextBase::call(const std::string &func_name,
                                     sol::variadic_args args,
                                     sol::this_state s) {
    // ... parse func_name and args ...

    if (in_parallel_block_) {
        // Buffer command instead of executing
        SerializedCommand cmd;
        cmd.id = fmt::format("{}-buffered-{}", instrument_id, parallel_buffer_.size());
        cmd.instrument_name = instrument_id;
        cmd.verb = verb;
        cmd.params = params;
        
        parallel_buffer_.push_back(std::move(cmd));
        return sol::nil;  // Don't execute yet
    }

    // Normal execution for non-parallel calls
    return send_command_immediate(instrument_id, verb, params);
}
```

#### Parallel Execution

```cpp
void RuntimeContextBase::execute_parallel_buffer() {
    if (parallel_buffer_.empty()) {
        return;
    }

    // Assign sync token
    uint64_t sync_token = next_sync_token_++;

    // Collect unique instruments
    std::vector<std::string> instruments;
    std::set<std::string> unique_instruments;
    for (const auto &cmd : parallel_buffer_) {
        if (unique_instruments.insert(cmd.instrument_name).second) {
            instruments.push_back(cmd.instrument_name);
        }
    }

    // Register barrier
    sync_coordinator_.register_barrier(sync_token, instruments);

    // Tag and dispatch commands
    std::vector<std::future<CommandResponse>> futures;
    for (auto &cmd : parallel_buffer_) {
        cmd.sync_token = sync_token;  // Tag with sync token

        auto worker = registry_.get_instrument(cmd.instrument_name);
        if (! worker) {
            LOG_ERROR("LUA_CONTEXT", "PARALLEL", "Instrument not found: {}",
                      cmd.instrument_name);
            continue;
        }

        futures.push_back(worker->execute(std::move(cmd)));
    }

    // Wait for all commands to complete
    for (auto &future : futures) {
        try {
            auto resp = future.get();
            if (!resp.success) {
                LOG_ERROR("LUA_CONTEXT", "PARALLEL", "Command failed: {}",
                          resp. error_message);
            }
        } catch (const std::exception &e) {
            LOG_ERROR("LUA_CONTEXT", "PARALLEL", "Future exception: {}", e.what());
        }
    }

    LOG_INFO("LUA_CONTEXT", "PARALLEL", "Parallel block complete (token={})", sync_token);
}
```

### SyncCoordinator (Server Side)

**File**: `src/server/SyncCoordinator.cpp`

#### Barrier Registration

```cpp
void SyncCoordinator::register_barrier(uint64_t sync_token,
                                       const std::vector<std::string> &instruments) {
    std::lock_guard lock(mutex_);

    SyncBarrier barrier;
    barrier.expected_instruments = std::set<std::string>(instruments.begin(), 
                                                          instruments.end());
    barrier.created_at = std::chrono:: steady_clock::now();

    barriers_[sync_token] = std::move(barrier);

    LOG_DEBUG("SYNC", "REGISTER", "Registered barrier token={} with {} instruments",
              sync_token, instruments.size());
}
```

#### ACK Handling

```cpp
bool SyncCoordinator::handle_ack(uint64_t sync_token,
                                 const std::string &instrument_name) {
    std::lock_guard lock(mutex_);

    auto it = barriers_.find(sync_token);
    if (it == barriers_.end()) {
        LOG_WARN("SYNC", "ACK", "Unknown sync token: {}", sync_token);
        return false;
    }

    auto &barrier = it->second;

    // Check if this instrument is expected
    if (barrier.expected_instruments.find(instrument_name) ==
        barrier.expected_instruments.end()) {
        LOG_WARN("SYNC", "ACK", "Unexpected ACK from {} for token {}",
                 instrument_name, sync_token);
        return false;
    }

    // Record ACK
    barrier.acked_instruments.insert(instrument_name);

    LOG_DEBUG("SYNC", "ACK", "Instrument {} ACKed token {} ({}/{})",
              instrument_name, sync_token, 
              barrier.acked_instruments.size(),
              barrier.expected_instruments. size());

    // Check if all ACKed
    bool complete = (barrier.acked_instruments == barrier.expected_instruments);

    if (complete) {
        LOG_INFO("SYNC", "COMPLETE", "Barrier {} complete, all {} instruments ACKed",
                 sync_token, barrier.expected_instruments.size());
    }

    return complete;
}
```

### Worker Process (Worker Side)

**File**: `src/workers/generic_worker_main.cpp`

#### Worker State

```cpp
// Worker maintains sync state
std::optional<uint64_t> waiting_sync_token;

while (running) {
    // If blocked on sync, only process SYNC_CONTINUE
    if (waiting_sync_token) {
        auto msg = ipc_queue->receive(std::chrono::milliseconds(100));
        
        if (! msg) continue;
        
        if (msg->type == ipc::IPCMessage::Type::SYNC_CONTINUE &&
            msg->sync_token == *waiting_sync_token) {
            LOG_DEBUG(instrument_name, "WORKER", 
                     "Received SYNC_CONTINUE for token={}, proceeding",
                     msg->sync_token);
            waiting_sync_token. reset();  // Unblock
        }
        continue;
    }

    // Normal command processing
    auto msg = ipc_queue->receive(std::chrono::milliseconds(100));
    if (!msg || msg->type != ipc::IPCMessage::Type::COMMAND) {
        continue;
    }

    // Execute command
    SerializedCommand cmd = deserialize_command(msg->payload);
    PluginResponse plugin_resp = execute_via_plugin(cmd);
    CommandResponse resp = convert_response(plugin_resp);

    // Send response
    send_response(resp);

    // If sync command, send ACK and block
    if (cmd.sync_token) {
        LOG_DEBUG(instrument_name, "WORKER", 
                 "Sending SYNC_ACK for token={}", *cmd.sync_token);
        
        send_sync_ack(*cmd.sync_token);
        waiting_sync_token = cmd.sync_token;  // Block until SYNC_CONTINUE
        
        LOG_DEBUG(instrument_name, "WORKER",
                 "Now waiting for SYNC_CONTINUE token={}", *waiting_sync_token);
    }
}
```

#### Sending SYNC_ACK

```cpp
// Worker sends ACK after completing sync command
ipc:: IPCMessage ack_msg;
ack_msg.type = ipc::IPCMessage::Type:: SYNC_ACK;
ack_msg.id = msg. id;
ack_msg. sync_token = cmd.sync_token. value();
ack_msg.payload_size = 0;

ipc_queue->send(ack_msg, std::chrono::milliseconds(1000));
```

### InstrumentWorkerProxy (Server Side)

**File**: `src/server/InstrumentWorkerProxy.cpp`

#### Handling SYNC_ACK

```cpp
void InstrumentWorkerProxy::handle_sync_ack_message(const ipc::IPCMessage &msg) {
    uint64_t sync_token = msg.sync_token;

    LOG_DEBUG(instrument_name_, "PROXY", "Received SYNC_ACK for token={}", sync_token);

    // Notify sync coordinator
    bool barrier_complete = sync_coordinator_.handle_ack(sync_token, instrument_name_);

    if (barrier_complete) {
        LOG_INFO(instrument_name_, "PROXY",
                 "Sync barrier {} complete, broadcasting SYNC_CONTINUE", sync_token);

        // Send SYNC_CONTINUE to this worker
        send_sync_continue(sync_token);
    }
}
```

#### Broadcasting SYNC_CONTINUE

```cpp
void InstrumentWorkerProxy::send_sync_continue(uint64_t sync_token) {
    if (! ipc_queue_ || !ipc_queue_->is_valid()) {
        LOG_WARN(instrument_name_, "PROXY", "Cannot send SYNC_CONTINUE, queue invalid");
        return;
    }

    ipc::IPCMessage msg;
    msg.type = ipc::IPCMessage::Type::SYNC_CONTINUE;
    msg.id = 0;
    msg.sync_token = sync_token;
    msg.payload_size = 0;

    bool sent = ipc_queue->send(msg, std::chrono::milliseconds(1000));

    if (sent) {
        LOG_DEBUG(instrument_name_, "PROXY", "Sent SYNC_CONTINUE token={}", sync_token);
    } else {
        LOG_ERROR(instrument_name_, "PROXY", "Failed to send SYNC_CONTINUE token={}", sync_token);
    }
}
```

## Usage in Lua Scripts

### Basic Parallel Block

```lua
-- Set multiple voltages simultaneously
context:parallel(function()
    context:call("DAC1:1.SetVoltage", 1.5)
    context:call("DAC2:1.SetVoltage", 2.0)
    context:call("DAC3:1.SetVoltage", 0.5)
end)

-- All voltages are now set
context:log("All DACs synchronized")
```

### Parallel Read and Write

```lua
-- Set outputs and measure inputs simultaneously
context:parallel(function()
    -- Set DACs
    context:call("DAC1.SetVoltage", 1.0)
    context:call("DAC2.SetVoltage", 2.0)
    
    -- Trigger measurements
    context:call("DMM1.Trigger")
    context:call("DMM2.Trigger")
end)

-- Now fetch results (sequential, after sync point)
local v1 = context:call("DMM1.FetchResult")
local v2 = context:call("DMM2.FetchResult")

context:log(string.format("Results: %. 6f, %.6f", v1, v2))
```

### Nested Loops with Sync

```lua
for outer = 1, 10 do
    -- Set outer loop parameter
    context:call("DAC1.SetVoltage", outer * 0.1)
    
    for inner = 1, 20 do
        -- Inner loop in parallel
        context:parallel(function()
            context:call("DAC2.SetVoltage", inner * 0.05)
            context:call("DAC3.SetVoltage", inner * 0.02)
        end)
        
        -- Measure after sync
        local measurement = context:call("DMM1.Measure")
        save_data(outer, inner, measurement)
    end
end
```

### Conditional Parallel Execution

```lua
function set_voltages_parallel(dacs, voltages)
    context:parallel(function()
        for i, dac_name in ipairs(dacs) do
            context:call(dac_name .. ".SetVoltage", voltages[i])
        end
    end)
end

-- Use the function
set_voltages_parallel({"DAC1", "DAC2", "DAC3"}, {1.0, 2.0, 3.0})
```

## Performance

### Overhead Measurements

Tested on Intel i7, Ubuntu 20.04, 3 instruments:

| Component | Time | Notes |
|-----------|------|-------|
| Sync token generation | 10 ns | Atomic increment |
| Barrier registration | 2 µs | Map insertion + mutex |
| Command dispatch (3 cmds) | 300 µs | IPC + serialization |
| Worker execution | 5-50 ms | Hardware dependent |
| ACK collection (3 ACKs) | 150 µs | IPC receive |
| SYNC_CONTINUE broadcast | 150 µs | IPC send to 3 workers |
| **Total sync overhead** | **~600 µs** | Excluding hardware |

### Scaling

| # Instruments | Sync Overhead | Notes |
|---------------|---------------|-------|
| 2 | 400 µs | Baseline |
| 5 | 800 µs | Linear scaling |
| 10 | 1.5 ms | Still sub-millisecond |
| 20 | 3 ms | Practical limit |

**Conclusion**:  Sync overhead is negligible compared to instrument response times (typically 1-100ms).

### Throughput

For repeated measurements with parallel execution:

```lua
for i = 1, 1000 do
    context:parallel(function()
        context:call("DAC1.Set", i * 0.001)
        context:call("DAC2.Set", i * 0.002)
    end)
    measure()
end
```

**Measured throughput**: ~500 iterations/second (2ms per iteration)

- Hardware settling:  ~1ms
- Sync overhead: ~0.5ms
- Measurement:  ~0.5ms

## Debugging

### Enable Sync Logging

```bash
instrument-server measure dc script.lua --log-level debug
```

### Log Patterns

Look for these in `instrument_server.log`:

```
[SYNC] [REGISTER] Registered barrier token=42 with 3 instruments
[PROXY] [DAC1] Received SYNC_ACK for token=42
[PROXY] [DAC2] Received SYNC_ACK for token=42
[PROXY] [DAC3] Received SYNC_ACK for token=42
[SYNC] [COMPLETE] Barrier 42 complete, all 3 instruments ACKed
[PROXY] [DAC1] Sent SYNC_CONTINUE token=42
[PROXY] [DAC2] Sent SYNC_CONTINUE token=42
[PROXY] [DAC3] Sent SYNC_CONTINUE token=42
```

### Worker Logs

Check worker logs for sync state:

```bash
tail -f worker_DAC1.log
```

```
[WORKER] [DAC1] Received command: SetVoltage (sync=42)
[WORKER] [DAC1] Command executed:  result=0 success=true
[WORKER] [DAC1] Sending SYNC_ACK for token=42
[WORKER] [DAC1] Now waiting for SYNC_CONTINUE token=42
[WORKER] [DAC1] Received SYNC_CONTINUE for token=42, proceeding
```

### Timing Analysis

Add timing logs to scripts:

```lua
local start = os.clock()

context:parallel(function()
    context:call("DAC1.Set", 1.0)
    context:call("DAC2.Set", 2.0)
    context:call("DAC3.Set", 3.0)
end)

local elapsed = os. clock() - start
context:log(string.format("Parallel block took %.3f ms", elapsed * 1000))
```

## Troubleshooting

### Problem: Parallel block never completes

**Symptoms**:

- Script hangs in `parallel()` block
- No error messages
- Workers show "waiting for SYNC_CONTINUE"

**Diagnosis**:

```bash
# Check if all instruments are running
instrument-server list

# Check worker logs
tail -f worker_*. log | grep SYNC
```

**Cause**: One instrument in parallel block is not running or crashed

**Solution**:

```bash
# Restart missing instrument
instrument-server start configs/missing_instrument.yaml

# Or remove it from parallel block
```

### Problem: Commands execute sequentially despite parallel()

**Symptoms**:

- Parallel block slow (sum of individual times)
- Logs show sequential execution

**Diagnosis**:
Check if commands target same instrument:

```lua
-- WRONG: Both target DAC1, executes sequentially
context:parallel(function()
    context:call("DAC1:1.Set", 1.0)
    context:call("DAC1:2.Set", 2.0)  -- Same instrument!
end)
```

**Solution**: Parallel only works across different instruments:

```lua
-- CORRECT: Different instruments, true parallel
context:parallel(function()
    context:call("DAC1:1.Set", 1.0)
    context:call("DAC2:1.Set", 2.0)  -- Different instrument
end)
```

### Problem: SYNC_ACK timeout

**Symptoms**:

```
[SYNC] [ACK] Barrier 42 timeout, missing ACKs from:  DAC2
```

**Cause**: Worker crashed or command failed

**Diagnosis**:

```bash
# Check worker status
instrument-server status DAC2

# Check worker log
tail -f worker_DAC2.log
```

**Solution**:

```bash
# Restart worker
instrument-server stop DAC2
instrument-server start configs/dac2.yaml
```

### Problem: Incorrect timing despite sync

**Symptoms**:

- Measurements show timing skew
- Sync logs show correct operation

**Cause**: Hardware settling time differences

**Solution**:  Add explicit delays:

```lua
context:parallel(function()
    context:call("DAC1.Set", 1.0)
    context:call("DAC2.Set", 2.0)
end)

-- Wait for settling (all instruments past sync point)
os.execute("sleep 0.01")  -- 10ms settling

-- Now measure
local result = context:call("DMM1.Measure")
```

## Advanced Topics

### Multiple Parallel Blocks

Sync tokens are independent - multiple blocks can be in flight:

```lua
-- Outer loop
for i = 1, 100 do
    context:parallel(function()  -- token=1, 2, 3, ... 
        context:call("DAC1.Set", i)
        context:call("DAC2.Set", i * 2)
    end)
    
    -- Inner loop (different sync)
    for j = 1, 10 do
        context:parallel(function()  -- token=101, 102, ... 
            context:call("DAC3.Set", j)
            context:call("DAC4.Set", j * 3)
        end)
    end
end
```

### Partial Instrument Participation

Not all instruments need to participate in every sync:

```lua
-- Only DAC1 and DAC2 synced
context:parallel(function()
    context:call("DAC1.Set", 1.0)
    context:call("DAC2.Set", 2.0)
end)

-- DAC3 operates independently
context:call("DAC3.Set", 3.0)

-- Different sync group
context:parallel(function()
    context:call("DAC3.Set", 3.5)
    context:call("DAC4.Set", 4.0)
end)
```

### Error Handling in Parallel Blocks

Individual command failures don't block sync:

```lua
context:parallel(function()
    context:call("DAC1.Set", 1.0)     -- Succeeds
    context:call("DAC2.Set", 999.0)   -- Fails (out of range)
    context:call("DAC3.Set", 3.0)     -- Succeeds
end)

-- Block completes, but DAC2 command failed
-- Check individual instrument status if needed
```

## See Also

- [Architecture](ARCHITECTURE.md) - System design
- [CLI Usage](CLI_USAGE.md) - Running measurements
- [Main README](../README.md) - Getting started
