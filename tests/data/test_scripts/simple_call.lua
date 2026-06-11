-- Simple single call test
context:log("Starting simple call test")

local cs = instrument_call_stack.new({
	instrument = "MockInstrument1",
	command = "ECHO",
})
local result = context:call(cs)
context:log("Result received")

if result then
	context:log("Test passed")
else
	context:log("Test failed")
end
