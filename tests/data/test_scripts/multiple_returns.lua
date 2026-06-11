-- Test collecting multiple return values of different types
context:log("Starting multiple returns test")

-- Test double return
local ds = instrument_call_stack.new({
	instrument = "MockInstrument1",
	command = "GET_DOUBLE",
})
local double_val = context:call(ds)
context:log(string.format("Double value: %s", tostring(double_val)))

-- Test string return
local ss = instrument_call_stack.new({
	instrument = "MockInstrument1",
	command = "GET_STRING",
})
local string_val = context:call(ss)
context:log(string.format("String value: %s", tostring(string_val)))

-- Test boolean return
local bs = instrument_call_stack.new({
	instrument = "MockInstrument1",
	command = "GET_BOOL",
})
local bool_val = context:call(bs)
context:log(string.format("Boolean value: %s", tostring(bool_val)))

-- Test array return
local as = instrument_call_stack.new({
	instrument = "MockInstrument1",
	command = "GET_ARRAY",
})
local array_val = context:call(as)
if type(array_val) == "table" then
	context:log(string.format("Array value: table with %d elements", #array_val))
else
	context:log(string.format("Array value: %s", tostring(array_val)))
end

-- Test SET command (should return true for success)
local set1 = instrument_call_stack.new({
	instrument = "MockInstrument1",
	command = "SET",
	channel = 1,
})
local set2 = instrument_call_stack.new({
	instrument = "MockInstrument1",
	command = "SET",
	channel = 2,
})
context:call(set1, 5.0)
context:call(set2, 3.0)

-- Test GET with channels
local get1 = instrument_call_stack.new({
	instrument = "MockInstrument1",
	command = "GET",
	channel = 1,
})
local get2 = instrument_call_stack.new({
	instrument = "MockInstrument1",
	command = "GET",
	channel = 2,
})
local v1 = context:call(get1)
local v2 = context:call(get2)

context:log(string.format("Channel 1: %s, Channel 2: %s", tostring(v1), tostring(v2)))

context:log("Multiple returns test complete")
