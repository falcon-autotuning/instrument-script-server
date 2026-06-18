-- Simple single call test
function main(ctx)
	ctx:log("Starting simple call test")

	local cs = instrument_call_stack.new({
		instrument = "MockInstrument1",
		command = "ECHO",
	})
	local result = ctx:call(cs)
	ctx:log("Result received")

	if result then
		ctx:log("Test passed")
	else
		ctx:log("Test failed")
	end
end
