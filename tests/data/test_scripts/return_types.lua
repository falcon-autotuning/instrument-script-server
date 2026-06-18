-- Test different return types
function main(ctx)
	ctx:log("Starting return types test")

	-- Double
	local cs = instrument_call_stack.new({
		instrument = "MockInstrument1",
		command = "GET_DOUBLE",
	})
	local num = ctx:call(cs)
	ctx:log(string.format("Double: %s (type: %s)", tostring(num), type(num)))

	-- String
	local cs = instrument_call_stack.new({
		instrument = "MockInstrument1",
		command = "GET_STRING",
	})
	local str = ctx:call(cs)
	ctx:log(string.format("String: %s (type: %s)", tostring(str), type(str)))

	-- Boolean
	local cs = instrument_call_stack.new({
		instrument = "MockInstrument1",
		command = "GET_BOOL",
	})
	local bool = ctx:call(cs)
	ctx:log(string.format("Boolean: %s (type: %s)", tostring(bool), type(bool)))

	ctx:log("Return types test complete")
end
