```markdown
# HTTP RPC for instrument-server

A lightweight HTTP/JSON RPC interface is available on the ServerDaemon to avoid repeated process creation when issuing many commands.

By default the daemon will listen on loopback port 8555 when RPC is configured. You can configure the port by:

- Setting environment variable INSTRUMENT_SERVER_RPC_PORT before starting the daemon, or
- Calling ServerDaemon::set_rpc_port(...) before starting the daemon (for programmatic usage).

Example (start daemon with RPC enabled):

```bash
export INSTRUMENT_SERVER_RPC_PORT=8555
instrument-server daemon start
```

Basic RPC request format (POST /rpc, JSON body):

```json
{
  "command": "list",
  "params": {}
}
```

Response format:

```json
{
  "ok": true,
  "instruments": [ "DAC1", "DMM1" ]
}
```

Supported commands (initial set)
- list
  - params: {}
  - returns: instruments array
- status
  - params: { "name": "<instrument-name>" }
  - returns: { "ok": true, "name": "...", "alive": true|false, "stats": { ... } }
- start
  - params: { "config_path": "/path/to/config.yaml" }
  - returns: ok true/false
- stop
  - params: { "name": "<instrument-name>" }
  - returns: ok true/false

Example using curl:

```bash
curl -X POST http://127.0.0.1:8555/rpc \
  -H "Content-Type: application/json" \
  -d '{"command":"list","params":{}}'
```

Notes
- The server binds to loopback only (127.0.0.1) by default.
- This is intended for local control and automation; consider adding authentication when exposing RPC to untrusted environments.
- The initial implementation focuses on a small set of commands. If you need more commands (measure, test, discover, plugins, etc.) we can safely extend the dispatch mapping to call existing code paths.
```
