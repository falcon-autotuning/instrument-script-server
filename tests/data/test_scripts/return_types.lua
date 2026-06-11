-- Test different return types
context:log("Starting return types test")

-- Double
local cs = instrument_call_stack.new({
	instrument = "MockInstrument1",
	command = "GET_DOUBLE",
})
local num = context:call(cs)
context:log(string.format("Double: %s (type: %s)", tostring(num), type(num)))

-- String
local cs = instrument_call_stack.new({
	instrument = "MockInstrument1",
	command = "GET_STRING",
})
local str = context:call(cs)
context:log(string.format("String: %s (type: %s)", tostring(str), type(str)))

-- Boolean
local cs = instrument_call_stack.new({
	instrument = "MockInstrument1",
	command = "GET_BOOL",
})
local bool = context:call(cs)
context:log(string.format("Boolean: %s (type: %s)", tostring(bool), type(bool)))

context:log("Return types test complete")
