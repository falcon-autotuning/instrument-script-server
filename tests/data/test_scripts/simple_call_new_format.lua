-- Simple single call test using new main function format
-- This demonstrates the Teal-compatible script structure

function main(ctx)
	-- Validate context parameter
	if not ctx then
		error("No context provided")
	end
	
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
