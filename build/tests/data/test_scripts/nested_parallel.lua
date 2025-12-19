-- Test nested loops with parallel blocks
context:log("Starting nested measurement")

for outer = 1, 3 do
	context:call("MockInstrument1.SET", outer)

	for inner = 1, 2 do
		context:parallel(function()
			context:call("MockInstrument2.SET", inner)
			context:call("MockInstrument3.SET", inner * 2)
		end)

		local result = context:call("MockInstrument1.MEASURE")
		context:log(string.format("Outer=%d, Inner=%d, Result=%s", outer, inner, tostring(result)))
	end
end

context:log("Nested measurement complete")
