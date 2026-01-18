-- Simple single call test using new main function format
-- This demonstrates the Teal-compatible script structure

function main(globals)
	-- Access context from globals or fallback to global context
	local ctx = globals or context
	
	ctx:log("Starting simple call test (new format)")
	
	local result = ctx:call("MockInstrument1.ECHO")
	ctx:log("Result received")
	
	if result then
		ctx:log("Test passed")
	else
		ctx:log("Test failed")
	end
	
	-- Explicit return (required for new format)
	return nil
end
