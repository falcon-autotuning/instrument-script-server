-- Test parallel execution
function main(ctx)
	ctx:log("Starting parallel test")

	local c1 = instrument_call_stack.new({
		instrument = "MockInstrument1",
		command = "ECHO",
	})
	local c2 = instrument_call_stack.new({
		instrument = "MockInstrument2",
		command = "ECHO",
	})
	ctx:parallel(function()
		ctx:call(c1)
		ctx:call(c2)
	end)

	ctx:log("Parallel test complete")
end
