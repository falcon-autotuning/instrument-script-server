-- Test error handling
function main(ctx)
	ctx:log("Starting error handling test")

	-- This should fail gracefully
	local cs = instrument_call_stack.new({
		instrument = "NonExistentInstrument",
		command = "COMMAND",
	})
	local result = ctx:call(cs)

	if not result then
		ctx:log("Error handled correctly")
	else
		ctx:log("Error handling failed")
	end

	ctx:log("Error handling test complete")
end
