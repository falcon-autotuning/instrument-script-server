-- Test passing table parameters
function main(ctx)
	ctx:log("Starting table parameters test")
	local cs = instrument_call_stack.new({
		instrument = "MockInstrument1",
		command = "CONFIGURE",
	})
	ctx:call(cs, {
		param1 = 1.5,
		param2 = "test",
		param3 = true,
	})

	ctx:log("Table parameters test complete")
end
