-- Test collecting multiple return values of different types with multi-channel instrument
function main(ctx)
	ctx:log("Starting multiple returns test")

	-- Test double return
	local ds = instrument_call_stack.new({
		instrument = "MockInstrumentMulti1",
		command = "GET_DOUBLE",
	})
	local double_val = ctx:call(ds)
	ctx:log(string.format("Double value: %s", tostring(double_val)))

	-- Test string return
	local ss = instrument_call_stack.new({
		instrument = "MockInstrumentMulti1",
		command = "GET_STRING",
	})
	local string_val = ctx:call(ss)
	ctx:log(string.format("String value: %s", tostring(string_val)))

	-- Test boolean return
	local bs = instrument_call_stack.new({
		instrument = "MockInstrumentMulti1",
		command = "GET_BOOL",
	})
	local bool_val = ctx:call(bs)
	ctx:log(string.format("Boolean value: %s", tostring(bool_val)))

	-- Test array return
	local as = instrument_call_stack.new({
		instrument = "MockInstrumentMulti1",
		command = "GET_ARRAY",
	})
	local array_val = ctx:call(as)
	if type(array_val) == "table" then
		ctx:log(string.format("Array value: table with %d elements", #array_val))
	else
		ctx:log(string.format("Array value: %s", tostring(array_val)))
	end

	-- Test SET command (should return true for success)
	local set1 = instrument_call_stack.new({
		instrument = "MockInstrumentMulti1",
		command = "SET",
		channel = 1,
	})
	local set2 = instrument_call_stack.new({
		instrument = "MockInstrumentMulti1",
		command = "SET",
		channel = 2,
	})
	ctx:call(set1, 5.0)
	ctx:call(set2, 3.0)

	-- Test GET with channels
	local get1 = instrument_call_stack.new({
		instrument = "MockInstrumentMulti1",
		command = "GET",
		channel = 1,
	})
	local get2 = instrument_call_stack.new({
		instrument = "MockInstrumentMulti1",
		command = "GET",
		channel = 2,
	})
	local v1 = ctx:call(get1)
	local v2 = ctx:call(get2)

	ctx:log(string.format("Channel 1: %s, Channel 2: %s", tostring(v1), tostring(v2)))

	ctx:log("Multiple returns test complete")
end
