function main(ctx, domain) -- A domain is a InstrumentDomain
	local sd = instrument_call_stack.new({
		instrument = "MockInstrument1",
		channel = 1,
		command = "SET",
	})
	ctx:call(sd, domain.get_min())
	ctx:call(sd, domain.get_max())
end
