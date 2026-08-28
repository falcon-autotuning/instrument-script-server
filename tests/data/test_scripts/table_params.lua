-- Test passing table parameters
function main(ctx)
	ctx:log("Starting table parameters test")
	local cs = instrument_call_stack.new({
		instrument = "MockInstrument1",
		command = "CONFIGURE",
	})
	ctx:call(cs, {
		config_param1 = 1.51,
		config_param2 = "test",
		config_param3 = true,
	})
	ctx:call(cs, {
		config_param1 = 23.0,
		config_param2 = "test",
		config_param3 = true,
	})
	ctx:call(cs, {
		config_param1 = -23.0,
		config_param2 = "test",
		config_param3 = true,
	})

	ctx:log("Table parameters test complete")
end
