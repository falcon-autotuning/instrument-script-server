-- Test measurement loop
context:log("Starting loop measurement")
local cs = instrument_call_stack.new({
	instrument = "MockInstrument1",
	command = "MEASURE",
})
for i = 1, 5 do
	local value = context:call(cs)
	context:log(string.format("Iteration %d: %s", i, tostring(value)))
end

context:log("Loop complete")
