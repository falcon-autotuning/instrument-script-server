-- Test array operations with buffer handles
function main(ctx, scale_factor)
  ctx:log(string.format("Starting array operations test with scale_factor: %s", tostring(scale_factor)))
  
  -- Call a command that returns an array (will return a BufferHandle)
  local array_buf = ctx:call("MockInstrument1.GET_ARRAY")
  ctx:log(string.format("Got array buffer: %s", tostring(array_buf)))
  
  if array_buf then
    ctx:log(string.format("Buffer ID: %s", array_buf:id()))
    ctx:log(string.format("Buffer size: %d", array_buf:size()))
    ctx:log(string.format("Buffer type: %s", array_buf:type()))
    
    -- Apply offset
    local offset_success = array_buf:add_offset(10.0)
    ctx:log(string.format("Add offset result: %s", tostring(offset_success)))
    
    -- Apply gain
    local gain_success = array_buf:multiply_gain(scale_factor or 2.0)
    ctx:log(string.format("Multiply gain result: %s", tostring(gain_success)))
  else
    ctx:log("ERROR: Expected array buffer but got nil")
  end
  
  ctx:log("Array operations test complete")
  return array_buf
end
