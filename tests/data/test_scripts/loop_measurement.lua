-- Test measurement loop
function main(ctx)
	ctx:log("Starting loop measurement")
	local cs = instrument_call_stack.new({
		instrument = "MockInstrument1",
		command = "MEASURE",
	})
	for i = 1, 5 do
		local value = ctx:call(cs)
		ctx:log(string.format("Iteration %d: %s", i, tostring(value)))
	end

	ctx:log("Loop complete")
end
