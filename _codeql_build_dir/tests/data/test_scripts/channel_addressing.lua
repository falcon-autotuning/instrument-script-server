-- Test channel addressing
context:log("Starting channel addressing test")

-- Test with channel number
context:call("MockInstrument1:1.SET", 5.0)
context:call("MockInstrument1:2.SET", 3.0)

local v1 = context:call("MockInstrument1:1.GET")
local v2 = context:call("MockInstrument1:2.GET")

context:log(string.format("Channel 1: %s, Channel 2: %s", tostring(v1), tostring(v2)))

context:log("Channel addressing test complete")
