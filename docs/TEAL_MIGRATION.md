# Teal-Compatible Measurement Script Migration Guide

## Overview

The instrument-script-server now supports a new script format designed for Teal static type checking and compilation. This guide explains the changes and how to migrate existing scripts.

## New Script Format

### Main Function Structure

Scripts should now define a `main` function that receives typed parameters:

```lua
-- Example: New Teal-compatible format
function main(globals)
    -- Access context from globals parameter
    local ctx = globals or context
    
    -- Your measurement code here
    ctx:log("Starting measurement")
    
    local result = ctx:call("INSTRUMENT.COMMAND", {param = value})
    
    if not result then
        ctx:error("Measurement failed")
        return nil
    end
    
    ctx:log("Measurement complete")
    return nil  -- Explicit return required
end
```

### Key Features

1. **Typed Parameters**: The `main(globals)` signature enables Teal type annotations
2. **Explicit Returns**: Main function must have a return statement
3. **Error Handling**: Use `context:error(message)` to report failures
4. **Result Collection**: All `context:call()` operations are automatically captured

## API Changes

### New Methods

#### `context:error(message)`
Reports an error from the measurement script. This sets an error state that will be included in the measurement response.

```lua
function main(globals)
    local ctx = globals or context
    
    if some_condition then
        ctx:error("Invalid configuration")
        return nil
    end
end
```

### Environment Variables

#### `INSTRUMENT_SCRIPT_SERVER_OPT_LUA_LIB`

Now supports multiple library paths separated by semicolons:

```bash
# Single path (directory or file)
export INSTRUMENT_SCRIPT_SERVER_OPT_LUA_LIB="/path/to/lua/libs"

# Multiple paths
export INSTRUMENT_SCRIPT_SERVER_OPT_LUA_LIB="/path/to/lib1;/path/to/lib2;/path/to/bundle.lua"
```

## Backward Compatibility

Scripts without a `main` function continue to work:

```lua
-- Old format: still supported
context:log("Starting measurement")
local result = context:call("INSTRUMENT.COMMAND")
```

The system automatically detects whether a script uses the old or new format.

## Migration Steps

### 1. Wrap Existing Code in Main Function

```lua
-- Before
context:log("Test")
local result = context:call("INSTRUMENT.GET_VALUE")

-- After
function main(globals)
    local ctx = globals or context
    ctx:log("Test")
    local result = ctx:call("INSTRUMENT.GET_VALUE")
    return nil
end
```

### 2. Update Global Variable Access

```lua
-- Before (globals injected directly)
local voltage = setVoltage
local rate = sampleRate

-- After (access from parameter)
function main(globals)
    local ctx = globals or context
    local voltage = globals.setVoltage or ctx.setVoltage
    local rate = globals.sampleRate or ctx.sampleRate
end
```

### 3. Add Error Handling

```lua
function main(globals)
    local ctx = globals or context
    
    local result = ctx:call("INSTRUMENT.MEASURE")
    
    if not result then
        ctx:error("Measurement returned no data")
        return nil
    end
    
    return nil
end
```

### 4. Add Explicit Returns

```lua
function main(globals)
    local ctx = globals or context
    -- measurement code
    return nil  -- Always include return statement
end
```

## Teal Type Definitions

When using Teal, you can define strict types for your measurement parameters:

```teal
-- Example Teal type definition
type MeasurementGlobals = record
    setVoltages: {number}
    sampleRate: number
    getters: {{string, number}}
    setters: {{string, number}}
end

-- Typed main function
function main(globals: MeasurementGlobals): nil
    local ctx = context
    ctx:log("Starting typed measurement")
    -- Type-safe measurement code
    return nil
end
```

## Error Handling

### Script-Level Errors

Use `context:error()` for expected error conditions:

```lua
function main(globals)
    local ctx = globals or context
    
    if not globals.voltage then
        ctx:error("Required parameter 'voltage' not provided")
        return nil
    end
end
```

### Lua Runtime Errors

Lua runtime errors (exceptions) are automatically captured and included in the response:

```lua
function main(globals)
    local ctx = globals or context
    
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
