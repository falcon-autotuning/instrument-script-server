-- Test nested loops with parallel blocks
context:log("Starting nested measurement")

local c1 = instrument_call_stack.new({
	instrument = "MockInstrument1",
	command = "SET",
})
local m1 = instrument_call_stack.new({
	instrument = "MockInstrument1",
	command = "MEASURE",
})
local c2 = instrument_call_stack.new({
	instrument = "MockInstrument2",
	command = "SET",
})
local c3 = instrument_call_stack.new({
	instrument = "MockInstrument3",
	command = "SET",
})
for outer = 1, 3 do
	context:call(c1, outer)

	for inner = 1, 2 do
		context:parallel(function()
			context:call(c2, inner)
			context:call(c3, inner * 2)
		end)

		local result = context:call(m1)
		context:log(string.format("Outer=%d, Inner=%d, Result=%s", outer, inner, tostring(result)))
	end
end

context:log("Nested measurement complete")
