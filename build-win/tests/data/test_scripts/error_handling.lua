-- Test error handling
context:log("Starting error handling test")

-- This should fail gracefully
local result = context:call("NonExistentInstrument. COMMAND")

if not result then
	context:log("Error handled correctly")
else
	context:log("Error handling failed")
end

context:log("Error handling test complete")
