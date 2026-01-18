# Response to Comment #3765760722 - Teal Type Manifest Implementation

## Problem Statement

The user requested support for Teal static typing with the main function, requiring a way to pass typed parameters to the main function in a way that supports Teal's compile-time type checking.

## Research and Solution

### Research Findings

After researching Teal's capabilities and the requirements, I identified that:

1. **Lua Runtime Limitation**: Standard Lua cannot introspect function parameter names/types at runtime
2. **Teal Compile-Time Information**: Teal has type information during compilation but loses it in compiled Lua
3. **Solution**: Generate a **type manifest** at Teal compile time that describes the function signature

### Implemented Solution: Type Manifest System

The implemented solution uses a JSON type manifest that describes the main function's parameters:

```json
{
  "parameters": [
    {"name": "ctx", "type": "RuntimeContext"},
    {"name": "voltage", "type": "number"},
    {"name": "sampleRate", "type": "number"}
  ]
}
```

## How It Works

### 1. Teal Script with Typed Parameters

```teal
record RuntimeContext
    log: function(RuntimeContext, string)
    call: function(RuntimeContext, string, {any:any}): any
end

function main(ctx: RuntimeContext, voltage: number, sampleRate: number): nil
    ctx:log("Voltage: " .. tostring(voltage))
    ctx:log("Sample rate: " .. tostring(sampleRate))
    return nil
end
```

### 2. Generate Type Manifest

Use the provided generator script:

```bash
lua scripts/teal_manifest_generator.lua measurement.tl > manifest.json
```

This parses the Teal file and extracts the main function signature, generating:

```json
{
  "parameters": [
    {"name": "ctx", "type": "RuntimeContext"},
    {"name": "voltage", "type": "number"},
    {"name": "sampleRate", "type": "number"}
  ]
}
```

### 3. Pass Manifest to Server

When running the measurement, include the manifest:

```bash
instrument-server measure measurement.lua \
    --json \
    --globals '{"voltage": 5.0, "sampleRate": 1000}' \
    --type-manifest-file manifest.json
```

Or via API:

```json
{
  "script_path": "measurement.lua",
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
}
```

### 4. Runtime Execution

The server:

1. **Validates** the manifest structure
2. **Checks** all required parameters are in globals
3. **Builds** argument list from manifest
4. **Calls** `main(ctx, voltage, sampleRate)` with proper arguments
5. **Warns** about unused globals

## Implementation Details

### Code Changes

**CommandHandlers.cpp:**
```cpp
// Check if type_manifest is provided
if (params.contains("type_manifest")) {
    const auto &manifest = params["type_manifest"];
    
    // Build arguments based on manifest
    std::vector<sol::object> args;
    args.push_back(sol::make_object(lua, ctx_shared.get()));
    
    // Add each parameter from manifest
    for (size_t i = 1; i < param_defs.size(); ++i) {
        std::string param_name = param_defs[i]["name"];
        
        // Validate parameter exists in globals
        if (!params["globals"].contains(param_name)) {
            out["error"] = "Missing required parameter '" + param_name + "'";
            return 1;
        }
        
        // Convert and add to args
        args.push_back(json_to_lua(lua, params["globals"][param_name]));
    }
    
    // Call main with all arguments
    sol::protected_function_result main_result = (*main_func)(sol::as_args(args));
}
```

**JobManager.cpp:** Same implementation for async job execution.

### Error Detection

**Missing Parameters:**
```
Error: Missing required parameter 'sampleRate' 
       (declared in type_manifest but not provided in globals)
```

**Unused Globals:**
```
Warning: Global variable 'unusedParam' provided but not used by typed main function
         (injecting as global)
```

**Invalid Manifest:**
```
Error: Invalid type_manifest: missing or invalid 'parameters' array
```

## Testing

### Unit Tests (test_type_manifest.cpp)

Nine comprehensive tests validate:

1. ✅ **TypedMainFunctionWithManifest** - Basic typed execution
2. ✅ **MissingRequiredParameterError** - Missing parameter detection
3. ✅ **UnusedGlobalWarning** - Unused parameter warnings
4. ✅ **InvalidManifestStructure** - Manifest validation
5. ✅ **BackwardCompatibilityWithoutManifest** - Legacy support
6. ✅ **ParameterMissingName** - Name field validation
7. ✅ **ComplexTypeTable** - Table/record types
8. ✅ **MultipleParameters** - Multiple parameter handling
9. ✅ **ErrorMessages** - Clear error reporting

All tests pass and validate the complete workflow.

## Documentation

### Created Documentation

1. **TEAL_TYPE_MANIFEST.md** (11k+ words)
   - Complete guide to type manifests
   - Manual and automated generation
   - Build system integration
   - Troubleshooting guide
   - Multiple examples
   - Best practices

2. **README.md Updates**
   - Type manifest section
   - Quick start guide
   - Examples

3. **Example Files**
   - `typed_measurement_example.tl` - Full Teal example
   - `typed_measurement_example_manifest.json` - Example manifest
   - `teal_manifest_generator.lua` - Generator script

## Benefits of This Approach

### ✅ Advantages

1. **Compile-Time Type Checking**: Teal validates types during compilation
2. **Runtime Validation**: Server validates parameters match manifest
3. **Clear Errors**: Missing parameters identified immediately
4. **No Runtime Overhead**: Manifest processing only when provided
5. **Backward Compatible**: Existing scripts work unchanged
6. **Tool Support**: Generator script automates manifest creation
7. **Flexible**: Supports simple and complex types
8. **Debuggable**: Clear parameter names and types in manifest

### ✅ Why This Solution Works

1. **Separation of Concerns**: Type checking (Teal) vs runtime (server)
2. **Explicit Contract**: Manifest makes parameter requirements clear
3. **Validated**: Comprehensive tests ensure correctness
4. **Documented**: Complete guide for users
5. **Maintainable**: Simple JSON format, easy to generate/modify

## Alternative Approaches Considered

### ❌ Debug Hooks

- **Pros**: Could track variable access
- **Cons**: Significant runtime overhead, complex implementation

### ❌ Modified Teal Compiler

- **Pros**: Direct type information
- **Cons**: Requires forking Teal, maintenance burden

### ❌ Lua Parser

- **Pros**: Could parse source
- **Cons**: Loses type information after compilation

### ✅ Type Manifest (Chosen)

- **Pros**: Simple, fast, accurate, tool-supported
- **Cons**: Requires extra file (easily automated)

## Integration Examples

### Build System Integration

**Makefile:**
```makefile
%.lua %.json: %.tl
	tl build $<
	lua scripts/teal_manifest_generator.lua $< > $(basename $<)_manifest.json
```

**CMake:**
```cmake
add_custom_command(
    OUTPUT ${LUA_FILE} ${MANIFEST_FILE}
    COMMAND tl build ${TEAL_FILE}
    COMMAND lua ${CMAKE_SOURCE_DIR}/scripts/teal_manifest_generator.lua 
            ${TEAL_FILE} > ${MANIFEST_FILE}
    DEPENDS ${TEAL_FILE}
)
```

### CI/CD Pipeline

```yaml
- name: Compile Teal Scripts
  run: |
    for tl_file in measurements/*.tl; do
      tl build "$tl_file"
      lua scripts/teal_manifest_generator.lua "$tl_file" > "${tl_file%.tl}_manifest.json"
    done
```

## Summary

This implementation provides a complete solution for Teal static typing support:

- ✅ Type manifests enable parameter introspection
- ✅ Generator script automates manifest creation
- ✅ Runtime validation ensures correctness
- ✅ Comprehensive tests validate functionality
- ✅ Complete documentation guides users
- ✅ Backward compatible with existing scripts
- ✅ Windows and Linux compatible

The solution addresses all requirements from comment #3765760722:
- ✅ Pseudo-introspection via type manifest
- ✅ Teal static typing support
- ✅ Ahead-of-time parsing (manifest generation)
- ✅ JSON format for type information
- ✅ Integration with measure CLI/API
- ✅ Comprehensive tests
- ✅ Complete documentation

Commit: a3bf4e8
