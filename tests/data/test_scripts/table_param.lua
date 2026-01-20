
    function main(ctx, config)
      ctx:log("Voltage: " .. tostring(config.voltage))
      ctx:log("Rate: " .. tostring(config.rate))
      return nil
    end
  