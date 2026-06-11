record MeasurementResponse
    instrument: function(self): string
    verb: function(self): string
    type: function(self): string
    value: function(self): any

    add_offset: function(self, number): MeasurementResponse
    multiply_gain: function(self, number): MeasurementResponse

    buffer: function(self): BufferHandle
end

record BufferHandle
    id: function(self): string
    size: function(self): integer
    type: function(self): string

    add_offset: function(self, number): boolean
    multiply_gain: function(self, number): boolean
end

-- See falcon-autotuning/instrument-call-stack for more information
record CallStack
end

record RuntimeContext
    log: function(self, string)
    error: function(self, string)
    call: function(self, CallStack, ...any): MeasurementResponse
    parallel: function(self, function())
end
