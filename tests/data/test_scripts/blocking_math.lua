-- Test blocking script parsing with math operations on results
context:log("Starting blocking math test")

-- Call a command that returns a MeasurementResponse
local voltage_resp = context:call("MockInstrument1.GET_DOUBLE")
local voltage = voltage_resp:value()  -- Extract the actual value
context:log(string.format("Got voltage: %s", tostring(voltage)))

-- Perform math on the result
local doubled = voltage * 2
context:log(string.format("Doubled: %s", tostring(doubled)))

-- Call another command and add them
local current_resp = context:call("MockInstrument2.MEASURE")
local current = current_resp:value()
context:log(string.format("Got current: %s", tostring(current)))

local sum = voltage + current
context:log(string.format("Sum: %s", tostring(sum)))

-- Or use the built-in math operations on MeasurementResponse
local adjusted = voltage_resp:add_offset(10.0):multiply_gain(2.0)
context:log(string.format("Adjusted voltage: %s", tostring(adjusted:value())))

context:log("Blocking math test complete")
