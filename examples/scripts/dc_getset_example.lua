---@type RuntimeContext_DCGetSet
---@param ctx RuntimeContext_DCGetSet
return function(ctx)
	ctx.log("Starting DC GetSet measurement")

	-- Configure setters in parallel
	ctx.parallel(function()
		for _, setter in ipairs(ctx.setters) do
			local instrument_id, channel = setter[1], setter[2]

			if instrument_id == "API1" then
				local voltage = ctx.setVoltages[channel] or 0.0
				SET_VOLTAGE(voltage)
			end
		end

		for _, getter in ipairs(ctx.getters) do
			local instrument_id, channel = getter[1], getter[2]

			if instrument_id == "GPI1" then
				SET_SAMPLE_RATE(channel, ctx.sampleRate)
			end
		end
	end)

	-- Acquire data in parallel
	ctx.parallel(function()
		for _, getter in ipairs(ctx.getters) do
			local instrument_id, channel = getter[1], getter[2]

			if instrument_id == "GPI1" then
				local data = GET_DATA(channel)
				IS_DATA_COLLECTION(data)
			end
		end
	end)

	RESET_COMPUTER()
	ctx.log("Measurement complete")
end
