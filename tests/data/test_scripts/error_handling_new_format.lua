-- Test error handling using new main function format
-- This demonstrates context:error() usage

function main(ctx)
	if not ctx then
		error("No context provided")
	end
	
	ctx:log("Starting error handling test (new format)")
	
	-- Test intentional error reporting
	local result = ctx:call("NonExistentInstrument.COMMAND")
	
	if not result then
		ctx:error("Expected error: instrument not found")
		return nil
	end
	
	ctx:log("Error handling test complete")
	return nil
end
