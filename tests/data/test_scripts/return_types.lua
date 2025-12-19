-- Test different return types
context:log("Starting return types test")

-- Double
local num = context:call("MockInstrument1.GET_DOUBLE")
context:log(string.format("Double: %s (type: %s)", tostring(num), type(num)))

-- String
local str = context:call("MockInstrument1.GET_STRING")
context:log(string.format("String: %s (type: %s)", tostring(str), type(str)))

-- Boolean
local bool = context:call("MockInstrument1.GET_BOOL")
context:log(string.format("Boolean: %s (type: %s)", tostring(bool), type(bool)))

context:log("Return types test complete")
