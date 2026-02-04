-- Demo Measurement Script
-- This demonstrates the instrument-script-server workflow

function main(ctx)
    ctx:log("Starting demo measurement")
    
    -- Query instrument identification
    local idn_resp = ctx:call("DemoInstrument.IDN")
    ctx:log("Instrument ID: " .. idn_resp:value())
    
    -- Set a voltage value
    ctx:log("Setting voltage to 5.0 V")
    ctx:call("DemoInstrument.SET", {voltage = 5.0})
    
    -- Read back the voltage
    local voltage_resp = ctx:call("DemoInstrument.GET")
    ctx:log("Voltage reading: " .. tostring(voltage_resp:value()) .. " V")
    
    -- Measure current
    local current_resp = ctx:call("DemoInstrument.MEASURE")
    ctx:log("Current measurement: " .. tostring(current_resp:value()) .. " A")
    
    -- Get a string value
    local string_resp = ctx:call("DemoInstrument.GET_STRING")
    ctx:log("String value: " .. string_resp:value())
    
    -- Get a boolean value
    local bool_resp = ctx:call("DemoInstrument.GET_BOOL")
    ctx:log("Boolean value: " .. tostring(bool_resp:value()))
    
    ctx:log("Demo measurement complete!")
    
    -- Return the final current measurement
    return {
        voltage = voltage_resp:value(),
        current = current_resp:value(),
        status = "success"
    }
end
