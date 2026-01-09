-- Test collecting multiple return values of different types
context:log("Starting multiple returns test")

-- Test double return
local double_val = context:call("MockInstrument1.GET_DOUBLE")
context:log(string.format("Double value: %s", tostring(double_val)))

-- Test string return
local string_val = context:call("MockInstrument1.GET_STRING")
context:log(string.format("String value: %s", tostring(string_val)))

-- Test boolean return
local bool_val = context:call("MockInstrument1.GET_BOOL")
context:log(string.format("Boolean value: %s", tostring(bool_val)))

-- Test array return
local array_val = context:call("MockInstrument1.GET_ARRAY")
if type(array_val) == "table" then
  context:log(string.format("Array value: table with %d elements", #array_val))
else
  context:log(string.format("Array value: %s", tostring(array_val)))
end

-- Test SET command (should return true for success)
context:call("MockInstrument1:1.SET", 5.0)
context:call("MockInstrument1:2.SET", 3.0)

-- Test GET with channels
local v1 = context:call("MockInstrument1:1.GET")
local v2 = context:call("MockInstrument1:2.GET")

context:log(string.format("Channel 1: %s, Channel 2: %s", tostring(v1), tostring(v2)))

context:log("Multiple returns test complete")
