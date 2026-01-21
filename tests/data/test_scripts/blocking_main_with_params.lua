-- Test blocking script with main function and parameters
function main(ctx, voltage_param)
  ctx:log(string.format("Starting main with voltage_param: %s", tostring(voltage_param)))
  
  -- Call measurement - returns MeasurementResponse
  local measured_resp = ctx:call("MockInstrument1.MEASURE")
  local measured = measured_resp:value()
  ctx:log(string.format("Measured: %s", tostring(measured)))
  
  -- Perform math with the parameter
  local scaled = measured * voltage_param
  ctx:log(string.format("Scaled result: %s", tostring(scaled)))
  
  -- Or use the built-in method
  local scaled_resp = measured_resp:multiply_gain(voltage_param)
  ctx:log(string.format("Scaled via method: %s", tostring(scaled_resp:value())))
  
  ctx:log("Main function complete")
  return scaled
end
