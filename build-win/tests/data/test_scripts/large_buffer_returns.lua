-- Test script for large buffer returns
-- This script tests that large data buffers are correctly captured and referenced

context:log("Starting large buffer returns test")

-- Call a command that returns a large data buffer
local buffer1 = context:call("TestScope.GET_LARGE_DATA")
context:log(string.format("Buffer 1 received: %s", tostring(buffer1)))

-- Call another command that returns a large data buffer
local buffer2 = context:call("TestScope.GET_LARGE_DATA")
context:log(string.format("Buffer 2 received: %s", tostring(buffer2)))

-- Verify we can still call regular commands
local small_val = context:call("TestScope.GET_SMALL_DATA")
context:log(string.format("Small data: %s", tostring(small_val)))

context:log("Large buffer returns test complete")
