-- Test blocking script with main function and parameters
function main(ctx, voltage_param)
  ctx:log(string.format("Starting main with voltage_param: %s", tostring(voltage_param)))
  
  -- Call measurement
  local measured = ctx:call("MockInstrument1.MEASURE")
  ctx:log(string.format("Measured: %s", tostring(measured)))
  
  -- Perform math with the parameter
  local scaled = measured * voltage_param
  ctx:log(string.format("Scaled result: %s", tostring(scaled)))
  
  ctx:log("Main function complete")
  return scaled
end
