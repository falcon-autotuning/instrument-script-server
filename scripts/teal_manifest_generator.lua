#!/usr/bin/env lua
-- teal_manifest_generator.lua
-- Generates type manifest JSON from Teal measurement scripts
-- Usage: lua teal_manifest_generator.lua measurement.tl

local json_encode

-- Simple JSON encoder (no external dependencies)
local function encode_value(val)
	local t = type(val)
	if t == "string" then
		return '"' .. val:gsub('"', '\\"') .. '"'
	elseif t == "number" or t == "boolean" then
		return tostring(val)
	elseif t == "table" then
		if #val > 0 then
			-- Array
			local items = {}
			for _, v in ipairs(val) do
				table.insert(items, encode_value(v))
			end
			return "[" .. table.concat(items, ",") .. "]"
		else
			-- Object
			local items = {}
			for k, v in pairs(val) do
				table.insert(items, '"' .. k .. '":' .. encode_value(v))
			end
			return "{" .. table.concat(items, ",") .. "}"
		end
	elseif val == nil then
		return "null"
	else
		error("Cannot encode type: " .. t)
	end
end

json_encode = encode_value

local function extract_main_params(content)
	local start_pos = content:find("function%s+main%s*%(")
	if not start_pos then
		return nil
	end

	local open_paren = content:find("%(", start_pos)
	if not open_paren then
		return nil
	end

	local depth = 1
	local i = open_paren + 1
	local len = #content
	local in_block_comment = false
	local in_line_comment = false

	while i <= len do
		local c = content:sub(i, i)
		local next2 = content:sub(i, i + 1)
		local prev2 = content:sub(i - 1, i)

		-- Enter block comment
		if not in_block_comment and not in_line_comment and next2 == "--" and content:sub(i + 2, i + 3) == "[[" then
			in_block_comment = true
			i = i + 4 -- skip '--[['
		-- Enter line comment
		elseif not in_block_comment and not in_line_comment and next2 == "--" then
			in_line_comment = true
			i = i + 2 -- skip '--'
		-- Exit block comment
		elseif in_block_comment and prev2 == "]]" then
			in_block_comment = false
			i = i + 1
		-- Exit line comment
		elseif in_line_comment and (c == "\n" or c == "\r") then
			in_line_comment = false
			i = i + 1
		-- Only count parens if not in comment
		elseif not in_block_comment and not in_line_comment then
			if c == "(" then
				depth = depth + 1
			elseif c == ")" then
				depth = depth - 1
				if depth == 0 then
					return content:sub(open_paren + 1, i - 1)
				end
			end
			i = i + 1
		else
			i = i + 1
		end
	end
	return nil
end
-- Parse Teal function signature
local function parse_teal_signature(teal_file)
	local file = io.open(teal_file, "r")
	if not file then
		error("Cannot open file: " .. teal_file)
	end

	local content = file:read("*all")
	file:close()

	-- Find main function signature
	-- Pattern: function main(param1: type1, param2: type2, ...): return_type
	local params_str = extract_main_params(content)

	if not params_str then
		io.stderr:write("Warning: No typed main function found in " .. teal_file .. "\n")
		io.stderr:write("Looking for pattern: function main(param: type, ...): return_type\n")
		return nil
	end

	-- Parse parameters
	local parameters = {}

	-- Split by comma, handling nested types
	local depth = 0
	local current_param = ""

	for i = 1, #params_str do
		local char = params_str:sub(i, i)

		if char == "{" then
			depth = depth + 1
			current_param = current_param .. char
		elseif char == "}" then
			depth = depth - 1
			current_param = current_param .. char
		elseif char == "," and depth == 0 then
			-- Parameter boundary
			local trimmed = current_param:match("^%s*(.-)%s*$")
			if #trimmed > 0 then
				local name, type = trimmed:match("^([%w_]+)%s*:%s*(.+)$")
				if name and type then
					table.insert(parameters, { name = name, type = type })
				end
			end
			current_param = ""
		else
			current_param = current_param .. char
		end
	end

	-- Process last parameter
	local trimmed = current_param:match("^%s*(.-)%s*$")
	if #trimmed > 0 then
		local name, type = trimmed:match("^([%w_]+)%s*:%s*(.+)$")
		if name and type then
			table.insert(parameters, { name = name, type = type })
		end
	end

	if #parameters == 0 then
		io.stderr:write("Warning: No parameters found in main function\n")
		return nil
	end

	return { parameters = parameters }
end

-- Main execution
local function main()
	if #arg < 1 then
		io.stderr:write("Usage: lua teal_manifest_generator.lua measurement.tl [output.json]\n")
		io.stderr:write("\nGenerates a type manifest from a Teal measurement script.\n")
		io.stderr:write("If output file is not specified, writes to stdout.\n")
		os.exit(1)
	end

	local teal_file = arg[1]
	local output_file = arg[2]

	-- Check if file exists
	local f = io.open(teal_file, "r")
	if not f then
		io.stderr:write("Error: File not found: " .. teal_file .. "\n")
		os.exit(1)
	end
	f:close()

	-- Parse Teal signature
	local manifest = parse_teal_signature(teal_file)

	if not manifest then
		io.stderr:write("Error: Could not parse main function signature\n")
		os.exit(1)
	end

	-- Generate JSON
	local json_output = json_encode(manifest)

	-- Write output
	if output_file then
		local out = io.open(output_file, "w")
		if not out then
			io.stderr:write("Error: Cannot write to file: " .. output_file .. "\n")
			os.exit(1)
		end
		out:write(json_output)
		out:write("\n")
		out:close()
		io.stderr:write("Manifest written to: " .. output_file .. "\n")
	else
		print(json_output)
	end

	-- Print summary to stderr
	io.stderr:write(string.format("Generated manifest with %d parameter(s)\n", #manifest.parameters))
	for i, param in ipairs(manifest.parameters) do
		io.stderr:write(string.format("  %d. %s: %s\n", i, param.name, param.type))
	end
end

-- Run
main()
