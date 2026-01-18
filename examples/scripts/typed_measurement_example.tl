-- Example Teal measurement script demonstrating typed parameters

-- Define RuntimeContext type
record RuntimeContext
    log: function(RuntimeContext, string)
    call: function(RuntimeContext, string, {any:any}): any
    error: function(RuntimeContext, string)
    parallel: function(RuntimeContext, function())
end

-- Define measurement configuration type
record MeasurementConfig
    setVoltage: number
    sampleRate: number
    numSamples: number
end

-- Main function with typed parameters
-- This signature will be parsed to generate the type manifest
function main(ctx: RuntimeContext, config: MeasurementConfig): nil
    ctx:log("Starting typed measurement")
    ctx:log(string.format("Voltage: %.2fV, Rate: %dHz, Samples: %d", 
                          config.setVoltage, config.sampleRate, config.numSamples))
    
    -- Set voltage using context:call
    ctx:call("PowerSupply.SET_VOLTAGE", {voltage = config.setVoltage})
    
    -- Configure sample rate
    ctx:call("Oscilloscope.SET_SAMPLE_RATE", {rate = config.sampleRate})
    
    -- Acquire data
    local data = ctx:call("Oscilloscope.ACQUIRE", {samples = config.numSamples})
    
    if not data then
        ctx:error("Failed to acquire data")
        return nil
    end
    
    ctx:log("Measurement complete")
    return nil
end
