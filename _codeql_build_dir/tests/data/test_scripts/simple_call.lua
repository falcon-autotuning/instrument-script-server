-- Simple single call test
context:log("Starting simple call test")

local result = context:call("MockInstrument1.ECHO")
context:log("Result received")

if result then
	context:log("Test passed")
else
	context:log("Test failed")
end
