-- Test Lua script for integration testing

print("Test script starting")

ctx = RuntimeContext_DCGetSet()
ctx.sampleRate = 1000
ctx.numPoints = 10

ctx:log("Running test measurement")

-- Simple test
ctx:parallel(function()
	ctx:log("In parallel block")
end)

ctx:log("Test complete")
