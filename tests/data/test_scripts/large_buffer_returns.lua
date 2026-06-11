-- Test script for large buffer returns
-- This script tests that large data buffers are correctly captured and referenced

context:log("Starting large buffer returns test")

-- Call a command that returns a large data buffer
local ld = instrument_call_stack.new({
	instrument = "TestScope",
	command = "GET_LARGE_DATA",
})
local sd = instrument_call_stack.new({
	instrument = "TestScope",
	command = "GET_SMALL_DATA",
})
local buffer1 = context:call(ld)
context:log(string.format("Buffer 1 received: %s", tostring(buffer1)))

-- Call another command that returns a large data buffer
local buffer2 = context:call(ld)
context:log(string.format("Buffer 2 received: %s", tostring(buffer2)))

-- Verify we can still call regular commands
local small_val = context:call(sd)
context:log(string.format("Small data: %s", tostring(small_val)))

context:log("Large buffer returns test complete")
