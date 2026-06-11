-- Test passing table parameters
context:log("Starting table parameters test")
local cs = instrument_call_stack.new({
	instrument = "MockInstrument1",
	command = "CONFIGURE",
})
context:call(cs, {
	param1 = 1.5,
	param2 = "test",
	param3 = true,
})

context:log("Table parameters test complete")
