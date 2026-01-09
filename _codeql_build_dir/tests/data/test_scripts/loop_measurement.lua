-- Test measurement loop
context:log("Starting loop measurement")

for i = 1, 5 do
	local value = context:call("MockInstrument1.MEASURE")
	context:log(string.format("Iteration %d: %s", i, tostring(value)))
end

context:log("Loop complete")
