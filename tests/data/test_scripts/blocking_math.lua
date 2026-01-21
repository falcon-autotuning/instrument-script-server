-- Test blocking script parsing with math operations on results
context:log("Starting blocking math test")

-- Call a command that returns a float
local voltage = context:call("MockInstrument1.GET_DOUBLE")
context:log(string.format("Got voltage: %s", tostring(voltage)))

-- Perform math on the result
local doubled = voltage * 2
context:log(string.format("Doubled: %s", tostring(doubled)))

-- Call another command and add them
local current = context:call("MockInstrument2.MEASURE")
context:log(string.format("Got current: %s", tostring(current)))

local sum = voltage + current
context:log(string.format("Sum: %s", tostring(sum)))

context:log("Blocking math test complete")
