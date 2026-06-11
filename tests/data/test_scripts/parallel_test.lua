-- Test parallel execution
context:log("Starting parallel test")

local c1 = instrument_call_stack.new({
	instrument = "MockInstrument1",
	command = "ECHO",
})
local c2 = instrument_call_stack.new({
	instrument = "MockInstrument2",
	command = "ECHO",
})
context:parallel(function()
	context:call(c1)
	context:call(c2)
end)

context:log("Parallel test complete")
