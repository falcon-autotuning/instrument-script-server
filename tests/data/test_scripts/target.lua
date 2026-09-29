function main(ctx, target) -- A target is a InstrumentTarget
	local ld = instrument_call_stack.new({
		instrument = target.get_instrument_name(),
		channel = target.get_channel(),
		channel_group = target.get_channel_group(),
		command = "GET",
	})
	ctx:call(ld)
end
