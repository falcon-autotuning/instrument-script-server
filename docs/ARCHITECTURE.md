# Instrument Server Architecture

Detailed technical documentation of the system architecture, components, and design decisions.

## Table of Contents

- [System Overview](#system-overview)
- [Process Model](#process-model)
- [Core Components](#core-components)
- [IPC Communication](#ipc-communication)
- [Synchronization System](#synchronization-system)
- [Plugin System](#plugin-system)
- [Configuration System](#configuration-system)
- [Data Flow](#data-flow)
- [Error Handling](#error-handling)
- [Performance Characteristics](#performance-characteristics)

## System Overview

The Instrument Server uses a **multi-process, daemon-based architecture** with IPC for communication and synchronization.

```
┌───────────────────────────────────────────────────────────────┐
│                      User Commands                            │
│  instrument-server start/stop/measure/...                     │
└───────────────────────────────────────────────────────────────┘
                          ↓
┌───────────────────────────────────────────────────────────────┐
│                   Server Daemon Process                       │
│                                                               │
│  ┌──────────────────────────────────────────────────────────┐ │
│  │ ServerDaemon                                             │ │
│  │  - Lifecycle management                                  │ │
│  │  - PID file handling                                     │ │
│  │  - Signal handling                                       │ │
│  └──────────────────────────────────────────────────────────┘ │
│                                                               │
│  ┌──────────────────────────────────────────────────────────┐ │
│  │ InstrumentRegistry                                       │ │
│  │  - Tracks all instruments                                │ │
│  │  - Creates InstrumentWorkerProxy per instrument          │ │
│  │  - Manages worker lifecycles                             │ │
│  └──────────────────────────────────────────────────────────┘ │
│                                                               │
│  ┌──────────────────────────────────────────────────────────┐ │
│  │ SyncCoordinator                                          │ │
│  │  - Manages sync barriers                                 │ │
│  │  - Tracks ACKs from workers                              │ │
│  │  - Broadcasts SYNC_CONTINUE                              │ │
│  └──────────────────────────────────────────────────────────┘ │
│                                                               │
│  ┌──────────────────────────────────────────────────────────┐ │
│  │ InstrumentWorkerProxy (per instrument)                   │ │
│  │  - IPC client                                            │ │
│  │  - Command serialization                                 │ │
│  │  - Response handling                                     │ │
│  │  - Heartbeat monitoring                                  │ │
│  └──────────────────────────────────────────────────────────┘ │
└───────────────────────────────────────────────────────────────┘
                          ↓ Shared Memory IPC
┌───────────────────────────────────────────────────────────────┐
│                    Worker Processes                           │
│                                                               │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐         │
│  │ instrument-  │  │ instrument-  │  │ instrument-  │         │
│  │ worker       │  │ worker       │  │ worker       │         │
│  │ (DMM1)       │  │ (DAC1)       │  │ (Scope1)     │         │
│  │              │  │              │  │              │         │
│  │ ┌──────────┐ │  │ ┌──────────┐ │  │ ┌──────────┐ │         │
│  │ │ Plugin   │ │  │ │ Plugin   │ │  │ │ Plugin   │ │         │
│  │ │ Loader   │ │  │ │ Loader   │ │  │ │ Loader   │ │         │
│  │ └──────────┘ │  │ └──────────┘ │  │ └──────────┘ │         │
│  └──────────────┘  └──────────────┘  └──────────────┘         │
└───────────────────────────────────────────────────────────────┘
                          ↓
┌───────────────────────────────────────────────────────────────┐
│                    Hardware Layer                             │
│  VISA • Serial • USB • Network • Native SDKs                  │
└───────────────────────────────────────────────────────────────┘
```

### Key Design Principles

1. **Process Isolation**: Each instrument runs in separate process - crashes don't affect others
2. **Daemon Model**: Long-lived background process manages state
3. **IPC Communication**: Fast shared memory queues for message passing
4. **Plugin Architecture**: Instrument drivers as loadable modules
5. **Synchronization**: Precise timing coordination via barrier protocol

## Process Model

### Server Daemon Process

**Purpose**: Long-lived background process managing all state

**Responsibilities**:

- Maintain InstrumentRegistry
- Coordinate SyncBarriers
- Handle user commands (via new invocations)
- Monitor worker health

**Lifecycle**:

```
daemon start → Create PID file → Initialize Registry → Event loop
                                                         ↓
daemon stop  ← Cleanup IPC ← Stop all workers ← Signal handling
```

**Implementation**:  `src/server/ServerDaemon.cpp`

### Worker Processes

**Purpose**: Isolated execution environment for each instrument

**Responsibilities**:

- Load and initialize plugin
- Execute commands from IPC queue
- Send heartbeats
- Handle sync protocol

**Lifecycle**:

```
registry.create_instrument() → fork() → load plugin → IPC loop
                                                        ↓
registry.remove_instrument() ← cleanup ← shutdown ← SIGTERM
```

**Implementation**: `src/workers/generic_worker_main.cpp`

### Command Processes

**Purpose**: Short-lived processes for user commands

**Responsibilities**:

- Parse command-line arguments
- Connect to daemon via registry
- Execute command
- Return result

**Lifecycle**:

```
instrument-server <cmd> → Connect to registry → Execute → Exit
```

**Implementation**: `src/tools/instrument_server_main.cpp`

## Core Components

### ServerDaemon

**File**: `src/server/ServerDaemon.cpp`

**Key Methods**:

```cpp
bool start();                    // Start daemon
void stop();                     // Stop daemon
bool is_running() const;         // Check if running
static bool is_already_running(); // Check for existing instance
static int get_daemon_pid();     // Get PID of running daemon
```

**State Management**:

- PID file: `/tmp/instrument-server-$USER/server. pid` (Linux)
- PID file: `%LOCALAPPDATA%\InstrumentServer\server.pid` (Windows)
- Singleton pattern ensures one instance

**Thread Model**:  Single-threaded event loop

### InstrumentRegistry

**File**: `src/server/InstrumentRegistry.cpp`

**Key Methods**:

```cpp
bool create_instrument(const std::string &config_path);
std::shared_ptr<InstrumentWorkerProxy> get_instrument(const std::string &name);
bool has_instrument(const std::string &name) const;
void remove_instrument(const std::string &name);
std::vector<std::string> list_instruments() const;
void stop_all();
```

**Data Structures**:

```cpp
class InstrumentRegistry {
private:
  mutable std::mutex mutex_;
  std::map<std::string, std::shared_ptr<InstrumentWorkerProxy>> instruments_;
  SyncCoordinator sync_coordinator_;
};
```

**Thread Safety**: All methods protected by mutex

### InstrumentWorkerProxy

**File**: `src/server/InstrumentWorkerProxy.cpp`

**Purpose**: Client-side IPC proxy for each instrument

**Key Methods**:

```cpp
bool start();                                    // Spawn worker
void stop();                                     // Stop worker
std::future<CommandResponse> execute(SerializedCommand cmd);
CommandResponse execute_sync(SerializedCommand cmd, timeout);
bool is_alive() const;                          // Check worker health
void send_sync_continue(uint64_t sync_token);   // Sync protocol
```

**Thread Model**:

- Main thread: Command dispatch
- Response thread: IPC message listener

**State**:

```cpp
class InstrumentWorkerProxy {
private:
  std::unique_ptr<ipc::SharedQueue> ipc_queue_;
  ipc::ProcessId worker_pid_;
  std::unordered_map<uint64_t, std:: promise<CommandResponse>> pending_responses_;
  std::thread response_thread_;
  std:: atomic<bool> running_;
};
```

### SyncCoordinator

**File**: `src/server/SyncCoordinator.cpp`

**Purpose**: Coordinate parallel execution across instruments

**Key Methods**:

```cpp
void register_barrier(uint64_t token, const std::vector<std:: string> &instruments);
bool handle_ack(uint64_t token, const std::string &instrument);
void clear_barrier(uint64_t token);
std::vector<std::string> get_waiting_instruments(uint64_t token) const;
```

**Data Structures**:

```cpp
class SyncCoordinator {
private:
  struct SyncBarrier {
    std::set<std::string> expected_instruments;
    std::set<std:: string> acked_instruments;
    std:: chrono::steady_clock::time_point created_at;
  };
  
  mutable std::mutex mutex_;
  std::map<uint64_t, SyncBarrier> barriers_;
};
```

**See Also**:  [SYNCHRONIZATION.md](SYNCHRONIZATION.md)

## IPC Communication

### Transport:  Shared Memory Queues

**Implementation**: `src/ipc/SharedQueue.cpp`

**Platform**:

- Linux:  POSIX shared memory (`shm_open`, `mmap`)
- Windows: Named shared memory (`CreateFileMapping`, `MapViewOfFile`)

**Queue Structure**:

```cpp
struct SharedQueue {
  // Ring buffer in shared memory
  // Lock-free for single producer, single consumer
  size_t head;
  size_t tail;
  IPCMessage messages[QUEUE_SIZE];
};
```

### Message Format

**File**: `include/instrument-server/ipc/IPCMessage.hpp`

```cpp
struct IPCMessage {
  enum class Type :  uint32_t {
    COMMAND = 1,
    RESPONSE = 2,
    HEARTBEAT = 3,
    SHUTDOWN = 4,
    SYNC_ACK = 5,
    SYNC_CONTINUE = 6
  };

  Type type;
  uint64_t id;           // Message ID
  uint64_t sync_token;   // Synchronization group
  uint32_t payload_size;
  char payload[4096];    // Serialized command/response (JSON)
};
```

### Serialization

**File**: `src/SerializedCommand.cpp`

Commands and responses serialized as JSON:

```cpp
std::string serialize_command(const SerializedCommand &cmd);
SerializedCommand deserialize_command(const std::string &json);

std::string serialize_response(const CommandResponse &resp);
CommandResponse deserialize_response(const std::string &json);
```

**Format Example**:

```json
{
  "id": "DMM1-12345",
  "instrument_name": "DMM1",
  "verb": "MEASURE",
  "expects_response": true,
  "timeout_ms": 5000,
  "sync_token": 42,
  "params": {
    "channel": 1,
    "range": "AUTO"
  }
}
```

## Synchronization System

**Full details**:  [SYNCHRONIZATION.md](SYNCHRONIZATION.md)

### Protocol Overview

1. **RuntimeContext** detects `parallel()` block
2. Commands buffered instead of immediate execution
3. All commands tagged with same `sync_token`
4. **SyncCoordinator** registers barrier with participating instruments
5. Commands dispatched to workers
6. Workers execute, send RESPONSE, then SYNC_ACK
7. Workers block until SYNC_CONTINUE received
8. **SyncCoordinator** waits for all ACKs
9. When complete, broadcasts SYNC_CONTINUE to all workers
10. Workers unblock and proceed to next command

### Timing Guarantees

- All instruments in parallel block complete before any advances
- No instrument can proceed past sync point until all ready
- Call blocks (sequential) execute on single instrument immediately

## Plugin System

**Full details**: [PLUGIN_DEVELOPMENT.md](PLUGIN_DEVELOPMENT.md)

### Plugin Interface

**File**: `include/instrument-server/plugin/PluginInterface.h`

```c
// Required exports
PluginMetadata plugin_get_metadata(void);
int32_t plugin_initialize(const PluginConfig *config);
int32_t plugin_execute_command(const PluginCommand *cmd, PluginResponse *resp);
void plugin_shutdown(void);
```

### Plugin Loader

**File**: `src/plugin/PluginLoader. cpp`

```cpp
class PluginLoader {
public:
  explicit PluginLoader(const std::string &path);
  bool is_loaded() const;
  PluginMetadata get_metadata();
  int32_t initialize(const PluginConfig &config);
  int32_t execute_command(const PluginCommand &cmd, PluginResponse &resp);
  void shutdown();
  
private:
  void *handle_;  // dlopen/LoadLibrary handle
  // Function pointers to plugin exports
};
```

### Plugin Registry

**File**: `src/plugin/PluginRegistry.cpp`

Maps protocol types to plugin paths:

```cpp
class PluginRegistry {
public:
  void discover_plugins(const std::vector<std::string> &search_paths);
  void register_plugin(const std::string &protocol, const std::string &path);
  std::string get_plugin_path(const std::string &protocol) const;
  std::vector<std::string> list_protocols() const;
  
private:
  std::map<std::string, std::string> plugins_; // protocol -> path
};
```

## Configuration System

### Instrument Configuration

**Format**:  YAML

```yaml
name: DMM1                    # Instrument instance name
api_ref: apis/keithley. yaml   # API definition file
connection: 
  type:  VISA                  # Protocol type (maps to plugin)
  address: "TCPIP::192.168.1.100:: INSTR"
  timeout:  5000
```

**Loading**: `InstrumentRegistry:: create_instrument()`

1. Load YAML → JSON
2. Load referenced API definition
3. Extract protocol type
4. Lookup plugin via PluginRegistry
5. Create InstrumentWorkerProxy
6. Spawn worker with plugin path

### API Definition

**Format**:  YAML

```yaml
protocol: 
  type:  VISA

commands:
  MEASURE:
    description: "Measure DC voltage"
    template: ": MEAS:VOLT:DC?"
    response_type: double
    
  SET_VOLTAGE:
    description: "Set output voltage"
    template: ": SOUR:VOLT {voltage}"
    params:
      voltage: 
        type: double
        required: true
        min: -10.0
        max: 10.0
```

**Validation**: `SchemaValidator` class validates against JSON schemas

## Data Flow

### Command Execution Flow

```
1.  Lua script:  context: call("DMM1.Measure")
      ↓
2. RuntimeContext. call()
      ↓
3. RuntimeContext.send_command()
      ↓
4. InstrumentRegistry.get_instrument("DMM1")
      ↓
5. InstrumentWorkerProxy.execute_sync()
      ↓
6. serialize_command() → JSON
      ↓
7. IPC:  Send to worker's queue
      ↓
8. Worker: Receive from queue
      ↓
9. deserialize_command() → SerializedCommand
      ↓
10. Convert to PluginCommand
      ↓
11. PluginLoader.execute_command()
      ↓
12. Plugin: Actual hardware communication
      ↓
13. PluginResponse → CommandResponse
      ↓
14. serialize_response() → JSON
      ↓
15. IPC: Send to server's queue
      ↓
16. InstrumentWorkerProxy:  Receive response
      ↓
17. deserialize_response() → CommandResponse
      ↓
18. Fulfill promise/future
      ↓
19. RuntimeContext:  Return to Lua
```

### Parallel Execution Flow

```
1. Lua: context:parallel(function() ... end)
      ↓
2. RuntimeContext. parallel()
   - Set in_parallel_block_ = true
      ↓
3. Execute Lua block
   - Each call() buffers command
      ↓
4. RuntimeContext.execute_parallel_buffer()
   - Generate sync_token
   - Register barrier with SyncCoordinator
      ↓
5. Dispatch all commands (with sync_token)
      ↓
6. Workers execute commands
      ↓
7. Workers send RESPONSE
      ↓
8. Workers send SYNC_ACK
      ↓
9. Workers BLOCK (wait for SYNC_CONTINUE)
      ↓
10. SyncCoordinator: Collect ACKs
      ↓
11. When all ACKed:  Broadcast SYNC_CONTINUE
      ↓
12. Workers receive SYNC_CONTINUE
      ↓
13. Workers unblock
      ↓
14. parallel() returns to Lua
```

## Error Handling

### Worker Death

```
Worker process crashes/dies
      ↓
Heartbeat timeout detected
      ↓
InstrumentWorkerProxy.is_alive() → false
      ↓
All pending promises failed with "Worker died"
      ↓
Error propagated to user
```

### Command Timeout

```
Command sent to worker
      ↓
future.wait_for(timeout)
      ↓
Timeout expires
      ↓
Return CommandResponse{success=false, error="Timeout"}
```

### Sync Timeout

```
Sync barrier registered
      ↓
Not all ACKs received within timeout
      ↓
Log error with missing instruments
      ↓
Force-complete barrier (broadcast SYNC_CONTINUE anyway)
      ↓
Prevents deadlock, but results may be invalid
```

### Plugin Error

```
Plugin.execute_command() returns error
      ↓
PluginResponse. success = false
      ↓
CommandResponse.success = false
      ↓
RuntimeContext.call() returns nil
      ↓
Lua script handles error
```

## Performance Characteristics

### Latency Measurements

| Operation | Typical Time | Notes |
|-----------|-------------|-------|
| IPC round-trip | 200 µs | Shared memory |
| Command serialization | 50 µs | JSON encoding |
| Sync coordination | 500 µs | Barrier overhead |
| Plugin call | 1-100 ms | Hardware dependent |

### Throughput

| Scenario | Rate | Bottleneck |
|----------|------|------------|
| Single command | ~1000 cmd/s | Instrument response |
| Pipelined (queue depth 10) | ~5000 cmd/s | IPC + serialization |
| Parallel (3 instruments) | ~3000 cmd/s total | Sync coordination |

### Scalability

- **Tested**: 20 simultaneous instruments
- **Memory**: ~50 MB per worker process
- **CPU**:  Minimal (event-driven, mostly I/O wait)
- **Limit**: OS process limit, shared memory availability

### Optimization Opportunities

1. **Binary serialization** instead of JSON (2-3x faster)
2. **Lock-free queues** for IPC (10-20% improvement)
3. **Command batching** (reduce IPC overhead)
4. **Thread pool** for response handling (higher concurrency)

## Platform-Specific Details

### Linux

- **IPC**: POSIX shared memory (`/dev/shm/`)
- **PID file**: `/tmp/instrument-server-$USER/server.pid`
- **Process creation**: `fork()` + `execl()`
- **Signals**:  SIGTERM for graceful shutdown

### Windows

- **IPC**: Named shared memory (`Global\instserver_*`)
- **PID file**: `%LOCALAPPDATA%\InstrumentServer\server.pid`
- **Process creation**: `CreateProcess()`
- **Termination**: `TerminateProcess()` with SIGTERM equivalent

## Future Enhancements

### Hot Reload

Restart worker without interrupting others:

```bash
instrument-server reload DMM1
```

**Implementation**: Track worker state, spawn new worker, transfer state

### Web Interface

HTTP API for remote control:

```http
POST /api/instruments/start
POST /api/measurements/run
GET  /api/instruments/status
```

**Implementation**: Embedded HTTP server in daemon

## See Also

- [CLI Usage](CLI_USAGE.md) - Command-line interface
- [Plugin Development](PLUGIN_DEVELOPMENT.md) - Writing plugins
- [Synchronization](SYNCHRONIZATION.md) - Parallel execution details
- [Main README](../README.md) - Getting started
