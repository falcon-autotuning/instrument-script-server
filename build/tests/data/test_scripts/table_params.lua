-- Test passing table parameters
context:log("Starting table parameters test")

context:call("MockInstrument1. CONFIGURE", {
	param1 = 1.5,
	param2 = "test",
	param3 = true,
})

context:log("Table parameters test complete")
