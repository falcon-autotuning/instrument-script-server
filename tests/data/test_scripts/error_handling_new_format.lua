-- Test error handling using new main function format
-- This demonstrates context:error() usage

function main(ctx)
	-- Critical errors use Lua error()
	if not ctx then
		error("No context provided - critical error")
	end
	
	ctx:log("Starting error handling test (new format)")
	
	-- Test intentional error reporting using ctx:error()
	local result = ctx:call("NonExistentInstrument.COMMAND")
	
	if not result then
		-- Use ctx:error() for measurement-level errors
		ctx:error("Expected error: instrument not found")
		return nil
	end
	
	ctx:log("Error handling test complete")
	return nil
end
