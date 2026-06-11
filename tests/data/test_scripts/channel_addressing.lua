-- Test channel addressing
context:log("Starting channel addressing test")

-- Test with channel number
local c1 = instrument_call_stack.new({
	instrument = "MockInstrument1",
	command = "SET",
	channel = 1,
})
local c2 = instrument_call_stack.new({
	instrument = "MockInstrument1",
	command = "SET",
	channel = 2,
})
local g1 = instrument_call_stack.new({
	instrument = "MockInstrument1",
	command = "SET",
	channel = 1,
})
local g2 = instrument_call_stack.new({
	instrument = "MockInstrument1",
	command = "SET",
	channel = 2,
})
context:call(c1, 5.0)
context:call(c2, 3.0)

local v1 = context:call(g1)
local v2 = context:call(g2)

context:log(string.format("Channel 1: %s, Channel 2: %s", tostring(v1), tostring(v2)))

context:log("Channel addressing test complete")
