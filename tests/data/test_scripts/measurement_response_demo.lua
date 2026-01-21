-- Demonstration of MeasurementResponse return type structure
-- This shows what ctx:call returns and how to use it

function main(ctx)
  ctx:log("===== MeasurementResponse Structure Demo =====")
  
  -- Get a float measurement
  local float_response = ctx:call("MockInstrument1.GET_DOUBLE")
  
  if float_response then
    ctx:log(string.format("Instrument: %s", float_response:instrument()))
    ctx:log(string.format("Verb: %s", float_response:verb()))
    ctx:log(string.format("Type: %s", float_response:type()))
    ctx:log(string.format("Value: %s", tostring(float_response:value())))
    
    -- Perform math operations
    local offset_result = float_response:add_offset(10.0)
    ctx:log(string.format("After add_offset(10.0): %s", tostring(offset_result:value())))
    
    local gain_result = offset_result:multiply_gain(2.0)
    ctx:log(string.format("After multiply_gain(2.0): %s", tostring(gain_result:value())))
  end
  
  ctx:log("===== Return Type Structure =====")
  ctx:log("MeasurementResponse {")
  ctx:log("  instrument() -> string  -- e.g. 'MockInstrument1'")
  ctx:log("  verb() -> string        -- e.g. 'GET_DOUBLE'")
  ctx:log("  type() -> string        -- 'float'|'integer'|'string'|'boolean'|'buffer'")
  ctx:log("  value() -> actual value -- number, string, boolean, or BufferHandle")
  ctx:log("  add_offset(offset) -> MeasurementResponse")
  ctx:log("  multiply_gain(gain) -> MeasurementResponse")
  ctx:log("}")
  
  return gain_result
end
