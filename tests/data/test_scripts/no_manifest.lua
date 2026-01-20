
    function main(ctx)
      -- Access globals directly (old way)
      local v = voltage or 0
      ctx:log("Voltage: " .. tostring(v))
      return nil
    end
  