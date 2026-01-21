# Teal-Compatible Measurement Script Migration Guide

## Overview

The instrument-script-server now uses blocking execution with structured return types for type safety and Teal static typing support. All measurement commands return `MeasurementResponse` objects with metadata.

## New Script Format

### Main Function Structure

Scripts should now define a `main` function that receives the runtime context:

```lua
-- Example: New Teal-compatible format with MeasurementResponse
function main(ctx, voltage)
    ctx:log("Starting measurement")
    
    -- ctx:call returns MeasurementResponse objects
    local current_resp = ctx:call("DMM.MEASURE")
    
    -- Extract the actual value
    local current = current_resp:value()
    
    -- Perform calculations
    local power = current * voltage
    
    -- Or use built-in math operations
    local adjusted = current_resp:add_offset(-0.001):multiply_gain(1.05)
    
    ctx:log(string.format("Power: %.6f W", power))
    return adjusted:value()
end
```

### Key Features

1. **Context Parameter**: The `main(ctx)` signature receives the runtime context
2. **MeasurementResponse**: All `ctx:call()` operations return structured objects with metadata
3. **Blocking Execution**: Commands execute synchronously and return actual values
4. **Math Operations**: Built-in `add_offset()` and `multiply_gain()` for signal processing
5. **Explicit Returns**: Main function must have a return statement
6. **Error Handling**: Use `ctx:error(message)` to report failures

## MeasurementResponse API

### Return Type Structure

```lua
-- MeasurementResponse object returned by ctx:call()
local response = ctx:call("INSTRUMENT.COMMAND")

-- Access metadata
response:instrument()  -- string: Instrument name
response:verb()        -- string: Command/verb name  
response:type()        -- string: "float"|"integer"|"string"|"boolean"|"buffer"

-- Extract value
response:value()       -- any: The actual measurement value

-- Math operations (for numeric types)
response:add_offset(offset)    -- MeasurementResponse: Add offset
response:multiply_gain(gain)   -- MeasurementResponse: Multiply by gain

-- For array types
response:buffer()      -- BufferHandle: Get buffer for array data
```

### Example Usage

**Scalar values:**

```lua
function main(ctx)
    -- Get measurement response
    local voltage_resp = ctx:call("DMM.MEASURE_VOLTAGE")
    
    -- Extract value for calculations
    local voltage = voltage_resp:value()
    
    -- Use in expressions
    if voltage > 5.0 then
        ctx:error("Voltage too high")
        return nil
    end
    
    -- Apply corrections using built-in methods
    local corrected = voltage_resp:add_offset(-0.01):multiply_gain(1.02)
    
    return corrected:value()
end
```

**Array buffers:**

```lua
function main(ctx)
    -- Get array measurement
    local waveform_resp = ctx:call("Scope.GET_WAVEFORM")
    
    -- Get buffer handle
    local buffer = waveform_resp:buffer()
    
    -- Apply signal processing
    buffer:add_offset(-0.5)      -- DC offset correction
    buffer:multiply_gain(10.0)   -- Amplification
    
    -- Buffer metadata
    ctx:log(string.format("Buffer has %d elements", buffer:size()))
    
    return waveform_resp
end
```

## API Changes

### New Methods

#### `ctx:call(command, args...)` → MeasurementResponse

Executes an instrument command and returns a structured response object:

```lua
local response = ctx:call("INSTRUMENT.COMMAND", param1, param2)
local value = response:value()
```

#### `MeasurementResponse:add_offset(offset)` → MeasurementResponse

Adds an offset to numeric measurement values:

```lua
local corrected = response:add_offset(-0.001)
local value = corrected:value()
```

#### `MeasurementResponse:multiply_gain(gain)` → MeasurementResponse

Multiplies numeric measurement values by a gain factor:

```lua
local scaled = response:multiply_gain(1.05)
local value = scaled:value()
```

#### `ctx:error(message)`

Reports an error from the measurement script:

```lua
function main(ctx)
    if some_condition then
        ctx:error("Invalid configuration")
        return nil
    end
end
```

## Backward Compatibility

Scripts without a `main` function continue to work but emit deprecation warnings:

```lua
-- Old format: still supported (deprecated)
context:log("Starting measurement")
local result = context:call("INSTRUMENT.COMMAND")
-- Warning: Returns raw values, not MeasurementResponse objects
```

**Warning**: Compatibility mode is deprecated and will be removed in a future version.

## Migration Steps

### 1. Wrap Existing Code in Main Function

```lua
-- Before
context:log("Test")
local result = context:call("INSTRUMENT.GET_VALUE")

-- After
function main(ctx)
    ctx:log("Test")
    local result_resp = ctx:call("INSTRUMENT.GET_VALUE")
    local result = result_resp:value()
    return result
end
```

### 2. Update Value Extraction

```lua
-- Before (old format - raw values)
local voltage = context:call("DMM.MEASURE")
local current = context:call("DMM.MEASURE_CURRENT")
local power = voltage * current

-- After (new format - MeasurementResponse)
function main(ctx)
    local voltage_resp = ctx:call("DMM.MEASURE")
    local current_resp = ctx:call("DMM.MEASURE_CURRENT")
    
    local voltage = voltage_resp:value()
    local current = current_resp:value()
    local power = voltage * current
    
    return power
end
```

### 3. Use Built-in Math Operations

```lua
function main(ctx)
    local raw_resp = ctx:call("INSTRUMENT.MEASURE")
    
    -- Chain operations
    local processed = raw_resp
        :add_offset(-0.5)     -- Remove DC offset
        :multiply_gain(10.0)  -- Apply gain
    
    return processed:value()
end
```

### 4. Add Error Handling

```lua
function main(ctx)
    local result_resp = ctx:call("INSTRUMENT.MEASURE")
    
    if result_resp:type() == "void" then
        ctx:error("Measurement returned no data")
        return nil
    end
    
    return result_resp:value()
end
```

## Teal Type Definitions

When using Teal, you can define strict types for the MeasurementResponse:

```teal
-- Type definitions for instrument-script-server

record MeasurementResponse
    instrument: function(MeasurementResponse): string
    verb: function(MeasurementResponse): string
    type: function(MeasurementResponse): string
    value: function(MeasurementResponse): any
    add_offset: function(MeasurementResponse, number): MeasurementResponse
    multiply_gain: function(MeasurementResponse, number): MeasurementResponse
    buffer: function(MeasurementResponse): BufferHandle
end

record BufferHandle
    id: function(BufferHandle): string
    size: function(BufferHandle): integer
    type: function(BufferHandle): string
    add_offset: function(BufferHandle, number): boolean
    multiply_gain: function(BufferHandle, number): boolean
end

record RuntimeContext
    log: function(RuntimeContext, string)
    call: function(RuntimeContext, string, ...any): MeasurementResponse
    error: function(RuntimeContext, string)
    parallel: function(RuntimeContext, function())
end

-- Typed main function
function main(ctx: RuntimeContext, voltage: number): number
    ctx:log("Starting typed measurement")
    
    -- Type-safe measurement with MeasurementResponse
    local current_resp: MeasurementResponse = ctx:call("DMM.MEASURE")
    local current: number = current_resp:value() as number
    
    -- Apply corrections
    local corrected: MeasurementResponse = current_resp
        :add_offset(-0.001)
        :multiply_gain(1.05)
    
    local power: number = (corrected:value() as number) * voltage
    
    return power
end
```

## Error Handling

### Script-Level Errors

Use `ctx:error()` for expected error conditions:

```lua
function main(ctx)
    local resp = ctx:call("INSTRUMENT.MEASURE")
    
    if resp:type() == "void" then
        ctx:error("Measurement returned no data")
        return nil
    end
    
    return resp:value()
end
```

### Lua Runtime Errors

Lua runtime errors (exceptions) are automatically captured and included in the response:

```lua
function main(ctx)
    -- This will be caught and reported
    error("Unexpected error")
end
```

Both types of errors appear in the measurement response with appropriate status and error messages.

## Response Format

### Successful Measurement

```json
{
    "ok": true,
    "script": "measurement.lua",
    "results": [
        {
            "index": 0,
            "instrument": "INSTRUMENT1",
            "verb": "MEASURE",
            "params": {},
            "executed_at_ms": 1234567890,
            "return": {
                "type": "float",
                "value": 3.14
            }
        }
    ]
}
```

### Failed Measurement

```json
{
    "ok": false,
    "error": "Measurement failed: no result",
    "script": "measurement.lua",
    "results": [...]
}
```

## Best Practices

1. **Always define main()**: Use the new format for all new scripts
2. **Validate inputs**: Check required parameters at the start of main()
3. **Use context:error()**: For expected error conditions
4. **Return explicitly**: Always include `return nil` or return value
5. **Type annotations**: Add Teal type annotations for compile-time checking

## Testing

Test scripts for both formats are available in `tests/data/test_scripts/`:

- `simple_call_new_format.lua`: Basic new format example
- `error_handling_new_format.lua`: Error handling example
- Original test scripts continue to work (backward compatibility)

## Implementation Notes

- Both sync (RPC) and async (JobManager) execution paths support the new format
- Lua libraries are loaded once and cached via `package.preload`
- Multiple library paths enable modular measurement library organization
- Error state is tracked independently from measurement results
