-- Test parallel execution
context:log("Starting parallel test")

context:parallel(function()
	context:call("MockInstrument1.ECHO")
	context:call("MockInstrument2.ECHO")
end)

context:log("Parallel test complete")
