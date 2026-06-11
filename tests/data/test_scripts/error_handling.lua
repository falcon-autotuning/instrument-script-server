-- Test error handling
context:log("Starting error handling test")

-- This should fail gracefully
local cs = instrument_call_stack.new({
	instrument = "NonExistentInstrument",
	command = "COMMAND",
})
local result = context:call(cs)

if not result then
	context:log("Error handled correctly")
else
	context:log("Error handling failed")
end

context:log("Error handling test complete")
