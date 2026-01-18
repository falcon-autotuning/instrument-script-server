-- Simple single call test using new main function format
-- This demonstrates the Teal-compatible script structure

function main(ctx)
	-- Validate context parameter (use Lua error for this critical check)
	if not ctx then
		error("No context provided - this is a critical error")
	end
	
	ctx:log("Starting simple call test (new format)")
	
	local result = ctx:call("MockInstrument1.ECHO")
	ctx:log("Result received")
	
	if result then
		ctx:log("Test passed")
	else
		-- Use ctx:error() for measurement-level errors
		ctx:error("Test failed - no result from ECHO command")
	end
	
	-- Explicit return (required for new format)
	return nil
end
