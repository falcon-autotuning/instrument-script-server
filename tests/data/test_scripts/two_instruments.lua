-- Two-instrument measurement test
-- Uses main(ctx) style; both instruments must be registered before running.
-- Uses ECHO which the mock VISA plugin handles explicitly for any instrument.
function main(ctx)
  ctx:log("Starting two-instrument measurement")

  local r1 = ctx:call(instrument_call_stack.new({
    instrument = "MockInstrument1",
    command    = "ECHO",
  }))
  ctx:log(string.format("MockInstrument1 echo: %s", tostring(r1)))

  local r2 = ctx:call(instrument_call_stack.new({
    instrument = "MockInstrument2",
    command    = "ECHO",
  }))
  ctx:log(string.format("MockInstrument2 echo: %s", tostring(r2)))

  ctx:log("Two-instrument measurement complete")
end
