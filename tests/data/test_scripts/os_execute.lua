function main(ctx)
	ctx:log("Starting os.execute + call test")

	os.execute("sleep 0.1")
	ctx:log("Pre-call sleep complete")

	local cs = instrument_call_stack.new({
		instrument = "MockInstrument1",
		command = "ECHO",
	})

	ctx:call(cs)
	ctx:log("Result received")

	os.execute("sleep 0.1")
	ctx:log("Post-call sleep complete")
end
