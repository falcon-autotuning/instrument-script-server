# Teal Type Manifest Generation Guide

## Overview

This guide explains how to generate a type manifest for Teal measurement scripts that enables static type checking and proper parameter passing to the `main` function.

## What is a Type Manifest?

A type manifest is a JSON document that describes the parameters of your main function, including their names and types. This allows the instrument-script-server to:

1. Validate that all required parameters are provided
2. Pass parameters directly to main() in the correct order
3. Detect unused global variables
4. Provide type information for debugging

## Teal Script Format

### Basic Structure

```teal
-- Define the RuntimeContext type
record RuntimeContext
    log: function(RuntimeContext, string)
    call: function(RuntimeContext, string, {any:any}): any
    error: function(RuntimeContext, string)
    parallel: function(RuntimeContext, function())
end

-- Define your main function with typed parameters
function main(ctx: RuntimeContext, voltage: number, sampleRate: number): nil
    ctx:log("Starting measurement")
    ctx:log("Voltage: " .. tostring(voltage))
    ctx:log("Sample rate: " .. tostring(sampleRate))
    
    -- Your measurement code here
    
    return nil
end
```

## Generating the Type Manifest

### Manual Generation

For simple scripts, you can manually create the type manifest JSON:

```json
{
  "parameters": [
    {"name": "ctx", "type": "RuntimeContext"},
    {"name": "voltage", "type": "number"},
    {"name": "sampleRate", "type": "number"}
  ]
}
```

### Automated Generation Script

For complex scripts or multiple measurements, use this Lua script to extract type information from your Teal files:

```lua
#!/usr/bin/env lua
-- teal_manifest_generator.lua
-- Usage: lua teal_manifest_generator.lua measurement.tl > manifest.json

local json = require("json")  -- You'll need a JSON library

function parse_teal_signature(teal_file)
    local file = io.open(teal_file, "r")
    if not file then
        error("Cannot open file: " .. teal_file)
    end
    
    local content = file:read("*all")
    file:close()
    
    -- Find main function signature
    local pattern = "function%s+main%(([^)]*)%)%s*:%s*([%w_]+)"
    local params_str = content:match(pattern)
    
    if not params_str then
        error("No main function found in " .. teal_file)
    end
    
    -- Parse parameters
    local parameters = {}
    for param in params_str:gmatch("([^,]+)") do
        local name, type = param:match("^%s*([%w_]+)%s*:%s*([%w_]+)%s*$")
        if name and type then
            table.insert(parameters, {name = name, type = type})
        end
    end
    
    return {parameters = parameters}
end

-- Main
if #arg < 1 then
    print("Usage: lua teal_manifest_generator.lua measurement.tl")
    os.exit(1)
end

local manifest = parse_teal_signature(arg[1])
print(json.encode(manifest))
```

### Using the Teal Compiler

Teal's compiler can be extended to output type information. Here's a custom build command:

```bash
#!/bin/bash
# compile_with_manifest.sh

TEAL_FILE=$1
LUA_FILE="${TEAL_FILE%.tl}.lua"
MANIFEST_FILE="${TEAL_FILE%.tl}_manifest.json"

# Compile Teal to Lua
tl build "$TEAL_FILE"

# Generate manifest (using the generator script)
lua teal_manifest_generator.lua "$TEAL_FILE" > "$MANIFEST_FILE"

echo "Compiled: $LUA_FILE"
echo "Manifest: $MANIFEST_FILE"
```

## Using the Type Manifest

### Via CLI

```bash
instrument-script-server measure measurement.lua \
    --json \
    --globals '{"voltage": 5.0, "sampleRate": 1000}' \
    --type-manifest-file manifest.json
```

### Via API

```bash
curl -X POST http://localhost:8555/measure \
    -H "Content-Type: application/json" \
    -d '{
        "script_path": "/path/to/measurement.lua",
        "globals": {
            "voltage": 5.0,
            "sampleRate": 1000
        },
        "type_manifest": {
            "parameters": [
                {"name": "ctx", "type": "RuntimeContext"},
                {"name": "voltage", "type": "number"},
                {"name": "sampleRate", "type": "number"}
            ]
        }
    }'
```

### Programmatic (Embedded API)

```cpp
json params;
params["script_path"] = "/path/to/measurement.lua";
params["globals"] = {
    {"voltage", 5.0},
    {"sampleRate", 1000}
};
params["type_manifest"] = {
    {"parameters", json::array({
        {{"name", "ctx"}, {"type", "RuntimeContext"}},
        {{"name", "voltage"}, {"type", "number"}},
        {{"name", "sampleRate"}, {"type", "number"}}
    })}
};

json out;
int result = handle_measure(params, out);
```

## Type Manifest Schema

### Structure

```json
{
  "parameters": [
    {
      "name": "param_name",
      "type": "type_name",
      "optional": false,
      "description": "Optional description"
    }
  ]
}
```

### Supported Types

- `RuntimeContext` - The measurement context (always first parameter)
- `number` - Numeric values (integers and floats)
- `string` - String values
- `boolean` - Boolean values
- `table` - Lua tables (complex types)
- `any` - Any type (for dynamic values)

### Example: Complex Types

```json
{
  "parameters": [
    {"name": "ctx", "type": "RuntimeContext"},
    {"name": "config", "type": "table", "description": "Configuration object with voltage and rate"},
    {"name": "channels", "type": "table", "description": "Array of channel numbers"},
    {"name": "enableLogging", "type": "boolean"}
  ]
}
```

## Error Handling

### Missing Required Parameters

If a parameter is declared in the type manifest but not provided in globals:

```
Error: Missing required parameter 'sampleRate' (declared in type_manifest but not provided in globals)
```

### Unused Globals

If a global is provided but not declared in the type manifest:

```
Warning: Global variable 'unusedParam' provided but not used by typed main function (injecting as global)
```

### Invalid Manifest

If the manifest structure is invalid:

```
Error: Invalid type_manifest: missing or invalid 'parameters' array
```

## Best Practices

### 1. Always Include Context

```teal
-- ✓ Correct
function main(ctx: RuntimeContext, voltage: number): nil
    -- ...
end

-- ✗ Wrong - context must be first parameter
function main(voltage: number, ctx: RuntimeContext): nil
    -- ...
end
```

### 2. Use Descriptive Parameter Names

```teal
-- ✓ Correct - clear parameter names
function main(ctx: RuntimeContext, outputVoltage: number, samplingRate: number): nil

-- ✗ Avoid - unclear names
function main(ctx: RuntimeContext, v: number, r: number): nil
```

### 3. Document Complex Types

```teal
-- Define custom types
record MeasurementConfig
    voltage: number
    rate: number
    channels: {number}
end

function main(ctx: RuntimeContext, config: MeasurementConfig): nil
    ctx:log("Voltage: " .. tostring(config.voltage))
    -- ...
end
```

### 4. Validate Parameters in Script

```teal
function main(ctx: RuntimeContext, voltage: number): nil
    -- Validate parameter ranges
    if voltage < 0 or voltage > 10 then
        ctx:error("Voltage must be between 0 and 10V")
        return nil
    end
    
    -- Your measurement code
end
```

## Integration with Build Systems

### Makefile Example

```makefile
%.lua %.json: %.tl
	tl build $<
	lua teal_manifest_generator.lua $< > $(basename $<)_manifest.json

measurements: measurement1.lua measurement2.lua measurement3.lua

clean:
	rm -f *.lua *_manifest.json
```

### CMake Example

```cmake
function(compile_teal_measurement TEAL_FILE)
    get_filename_component(BASE_NAME ${TEAL_FILE} NAME_WE)
    set(LUA_FILE "${CMAKE_CURRENT_BINARY_DIR}/${BASE_NAME}.lua")
    set(MANIFEST_FILE "${CMAKE_CURRENT_BINARY_DIR}/${BASE_NAME}_manifest.json")
    
    add_custom_command(
        OUTPUT ${LUA_FILE} ${MANIFEST_FILE}
        COMMAND tl build ${TEAL_FILE}
        COMMAND lua ${CMAKE_SOURCE_DIR}/scripts/teal_manifest_generator.lua 
                ${TEAL_FILE} > ${MANIFEST_FILE}
        DEPENDS ${TEAL_FILE}
        COMMENT "Compiling Teal measurement: ${TEAL_FILE}"
    )
    
    add_custom_target(${BASE_NAME}_measurement 
        DEPENDS ${LUA_FILE} ${MANIFEST_FILE})
endfunction()

compile_teal_measurement(measurements/dc_voltage.tl)
compile_teal_measurement(measurements/frequency_sweep.tl)
```

## Troubleshooting

### "No main function found"

Ensure your Teal script defines a `main` function:

```teal
function main(ctx: RuntimeContext): nil
    -- Your code
    return nil
end
```

### "Parameter type mismatch"

The manifest describes the expected types. Ensure the globals match:

```json
// Manifest says number
{"name": "voltage", "type": "number"}

// But you're passing string
{"globals": {"voltage": "5.0"}}  // ✗ Wrong

// Pass number instead
{"globals": {"voltage": 5.0}}  // ✓ Correct
```

### "Backward Compatibility Issues"

To support both old and new formats:

```teal
-- New format with types
function main(ctx: RuntimeContext, voltage: number): nil
    -- Measurement code
end

-- Also works without manifest (legacy)
-- Globals will be injected automatically
```

## Examples

### Simple Measurement

**measurement_simple.tl:**
```teal
record RuntimeContext
    log: function(RuntimeContext, string)
    call: function(RuntimeContext, string, {any:any}): any
end

function main(ctx: RuntimeContext, voltage: number): nil
    ctx:log("Setting voltage: " .. tostring(voltage))
    ctx:call("PowerSupply.SET_VOLTAGE", {voltage = voltage})
    return nil
end
```

**manifest:**
```json
{
  "parameters": [
    {"name": "ctx", "type": "RuntimeContext"},
    {"name": "voltage", "type": "number"}
  ]
}
```

### Complex Measurement

**measurement_complex.tl:**
```teal
record RuntimeContext
    log: function(RuntimeContext, string)
    call: function(RuntimeContext, string, {any:any}): any
    parallel: function(RuntimeContext, function())
end

record ChannelConfig
    id: number
    voltage: number
    current_limit: number
end

function main(ctx: RuntimeContext, channels: {ChannelConfig}, duration: number): nil
    ctx:log("Configuring " .. tostring(#channels) .. " channels")
    
    ctx:parallel(function()
        for _, ch in ipairs(channels) do
            ctx:call("PowerSupply:Channel" .. ch.id .. ".SET_VOLTAGE", 
                    {voltage = ch.voltage})
            ctx:call("PowerSupply:Channel" .. ch.id .. ".SET_CURRENT_LIMIT", 
                    {current = ch.current_limit})
        end
    end)
    
    ctx:log("Waiting " .. tostring(duration) .. "s")
    -- Measurement logic here
    
    return nil
end
```

**manifest:**
```json
{
  "parameters": [
    {"name": "ctx", "type": "RuntimeContext"},
    {"name": "channels", "type": "table", "description": "Array of channel configurations"},
    {"name": "duration", "type": "number", "description": "Measurement duration in seconds"}
  ]
}
```

## Conclusion

The type manifest system enables:
- ✓ Static type checking with Teal
- ✓ Clear parameter contracts
- ✓ Better error messages
- ✓ Runtime type validation
- ✓ Backward compatibility

For questions or issues, see the main [TEAL_MIGRATION.md](TEAL_MIGRATION.md) guide.
