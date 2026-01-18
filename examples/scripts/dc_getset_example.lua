-- DC GetSet Measurement Script
-- This script demonstrates the new main function format for Teal compatibility
-- The main function receives injected globals as a parameter

-- Helper function for safe table access with defaults
local function safe_get(tbl, key, default)
	if not tbl then return default end
	return tbl[key] or default
end

---@param globals table Injected runtime context with measurement parameters
---@return table|nil results Returns measurement results or nil on success
function main(globals)
	-- Access context from globals
	local ctx = globals or context
	
	-- Validate we have the required context
	if not ctx then
		if context then
			ctx = context
		else
			error("No context available")
		end
	end
	
	ctx.log("Starting DC GetSet measurement")

	-- Configure setters in parallel
	ctx.parallel(function()
		for _, setter in ipairs(ctx.setters or {}) do
			local instrument_id, channel = setter[1], setter[2]

			if instrument_id == "API1" then
				local voltage = safe_get(ctx.setVoltages, channel, 0.0)
				SET_VOLTAGE(voltage)
			end
		end

		for _, getter in ipairs(ctx.getters or {}) do
			local instrument_id, channel = getter[1], getter[2]

			if instrument_id == "GPI1" then
				SET_SAMPLE_RATE(channel, ctx.sampleRate or 1000)
			end
		end
	end)

	-- Acquire data in parallel
	ctx.parallel(function()
		for _, getter in ipairs(ctx.getters or {}) do
			local instrument_id, channel = getter[1], getter[2]

			if instrument_id == "GPI1" then
				local data = GET_DATA(channel)
				IS_DATA_COLLECTION(data)
			end
		end
	end)

	RESET_COMPUTER()
	ctx.log("Measurement complete")
	
	-- Results are collected implicitly via context:call()
	-- Return value is optional
	return nil
end
