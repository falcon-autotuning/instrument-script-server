---@meta

--- InstrumentTarget represents a reference to an instrument, optionally with a channel.
---@class InstrumentTarget
---@field id string Instrument identifier (e.g., "GPI1", "API1")
---@field channel? number Optional channel number (if applicable)

InstrumentTarget = {}

--- Serializes an InstrumentTarget to a canonical string key.
--- The format is "id" if no channel is present, or "id:channel" if channel is specified.
---@param target InstrumentTarget
---@return string
function InstrumentTarget:serialize(target)
	if target.channel ~= nil then
		return string.format("%s:%d", target.id, target.channel)
	else
		return target.id
	end
end
