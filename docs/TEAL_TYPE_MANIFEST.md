# Teal Type Manifests

## Overview

Instrument Script Server executes **Lua scripts**.  
If you use **Teal**, you must compile `.tl → .lua` before running.

A **type manifest** optionally describes the parameters of your `main()` function so the server can:

- Validate input parameters
- Pass arguments in the correct order
- Detect unused or missing values

---

## Basic Usage

### 1. Define your `main` function in Teal

```teal
function main(ctx: RuntimeContext, voltage: number, rate: number): nil
    ctx:log("Voltage: " .. tostring(voltage))
end
````

***

### 2. Compile to Lua

```bash
tl build measurement.tl
```

***

### 3. Provide a type manifest

```json
{
  "parameters": [
    {"name": "ctx", "type": "RuntimeContext"},
    {"name": "voltage", "type": "number"},
    {"name": "rate", "type": "number"}
  ]
}
```

***

### 4. Run the script

```bash
instrument-script-server measure measurement.lua \
    --globals '{"voltage": 5.0, "rate": 1000}' \
    --type-manifest-file manifest.json
```

***

## Notes

- `ctx` must be the first parameter
- Parameters are matched by name and passed to `main()`
- Without a manifest, globals are still injected (legacy behavior)
- For the CallStack type, it expects on input the instrument_call_stack_serialize

***

## Supported Types

- `number`
- `string`
- `boolean`
- `table`
- `RuntimeContext`
- `CallStack`

***

## Important

ISS does **not execute Teal directly**:

- Write Teal for type safety
- Compile to Lua before running
- Use manifests for runtime validation

***

## More Information

- [Teal Type Definitions (`iss.d.tl`)](https://github.com/falcon-autotuning/instrument-script-server/blob/main/scripts/types/iss.d.tl)
