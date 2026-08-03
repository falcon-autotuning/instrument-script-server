-- Test channel addressing with multi-channel instrument
function main(ctx)
	ctx:log("Starting channel addressing test")

	-- Test with channel number
	local c1 = instrument_call_stack.new({
		instrument = "MockInstrumentMulti1",
		command = "SET",
		channel = 1,
	})
	local c2 = instrument_call_stack.new({
		instrument = "MockInstrumentMulti1",
		command = "SET",
		channel = 2,
	})
	local g1 = instrument_call_stack.new({
		instrument = "MockInstrumentMulti1",
		command = "GET",
		channel = 1,
	})
	local g2 = instrument_call_stack.new({
		instrument = "MockInstrumentMulti1",
		command = "GET",
		channel = 2,
	})
	ctx:call(c1, 5.0)
	ctx:call(c2, 3.0)

	local v1 = ctx:call(g1)
	local v2 = ctx:call(g2)

	ctx:log(string.format("Channel 1: %s, Channel 2: %s", tostring(v1), tostring(v2)))

	ctx:log("Channel addressing test complete")
end
