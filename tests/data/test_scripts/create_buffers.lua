function main(ctx)
    ctx:log("Starting buffer creation script")
    local ld = instrument_call_stack.new({
        instrument = "TestScope",
        command = "GET_LARGE_DATA",
    })
    local buffer = ctx:call(ld)
    ctx:log("Created buffer: " .. tostring(buffer))
end
