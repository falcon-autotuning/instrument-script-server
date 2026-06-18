-- Test nested loops with parallel blocks
function main(ctx)
	ctx:log("Starting nested measurement")

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
	for outer = 1.0, 3.0 do
		ctx:call(c1, outer)

		for inner = 1.0, 2.0 do
			ctx:parallel(function()
				ctx:call(c2, inner)
				ctx:call(c3, inner * 2)
			end)

			local result = ctx:call(m1)
			ctx:log(string.format("Outer=%d, Inner=%d, Result=%s", outer, inner, tostring(result)))
		end
	end

	ctx:log("Nested measurement complete")
end
