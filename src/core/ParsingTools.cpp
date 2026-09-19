#include "instrument-script-server/core/ParsingTools.hpp"
#include <absl/strings/str_format.h>
#include <format>
#include <instrument-plugin.h>
#include <iostream>
#include <stdexcept>
#include <unordered_set>
#include <yaml-cpp/yaml.h>
namespace instserver {
namespace {
static Notation parseNotation(const YAML::Node &node) {
  const auto value = node.as<std::string>();

  if (value == "auto") {
    return Notation::Auto;
  }
  if (value == "fixed") {
    return Notation::Fixed;
  }
  if (value == "scientific") {
    return Notation::Scientific;
  }
  if (value == "engineering") {
    return Notation::Engineering;
  }

  throw std::runtime_error("Invalid notation: " + value);
}
void unpack_format(const YAML::Node &node, IO *io) {
  const YAML::Node &format = node["format"];
  if (format["decimal_places"]) {
    io->form.decimal_places = format["decimal_places"].as<int>();
  }
  if (format["significant_digits"]) {
    io->form.significant_digits = format["significant_digits"].as<int>();
  }
  if (format["notation"]) {
    io->form.notation = parseNotation(format["notation"]);
  }
  if (format["exponent_character"]) {
    io->form.exponent_char = format["exponent_character"].as<char>();
  }
  if (format["high_representation"]) {
    io->form.high_representation =
        format["high_representation"].as<std::string>();
  }
  if (format["low_representation"]) {
    io->form.low_representation =
        format["low_representation"].as<std::string>();
  }
}
void unpack_precision(const YAML::Node &node, IO *io) {
  const YAML::Node &precision = node["precision"];

  if (precision["resolution"]) {
    io->precision.resolution = precision["resolution"].as<double>();
  }
}
void unpack_minmax(const YAML::Node &node, IO *io, const std::string &minmax) {
  if (!node[minmax]) {
    return;
  }

  auto &dest = (minmax == "min") ? io->min : io->max;

  if (io->type == PARAM_TYPE_DOUBLE) {
    dest = node[minmax].as<double>();
  } else if (io->type == PARAM_TYPE_INT64) {
    dest = node[minmax].as<int64_t>();
  }
}
void unpack_min(const YAML::Node &node, IO *io) {
  unpack_minmax(node, io, "min");
}
void unpack_max(const YAML::Node &node, IO *io) {
  unpack_minmax(node, io, "max");
}
void unpack_role(const YAML::Node &node, IO *io) {
  if (!node["role"]) {
    return;
  }
  auto raw = node["role"].as<std::string>();
  if (raw == "input") {
    io->role = Role::Input;
  } else if (raw == "output") {
    io->role = Role::Output;
  } else if (raw == "inout") {
    io->role = Role::InOut;
  } else if (raw == "trigger-in") {
    io->role = Role::TriggerIn;
  } else if (raw == "trigger-out") {
    io->role = Role::TriggerOut;
  } else if (raw == "clock-in") {
    io->role = Role::ClockIn;
  } else if (raw == "clock-out") {
    io->role = Role::ClockOut;
  } else if (raw == "setting") {
    io->role = Role::Setting;
  } else {
    io->role = std::nullopt;
  }
}
void unpack_unit(const YAML::Node &node, IO *io) {
  if (!node["unit"]) {
    return;
  }
  io->unit = node["unit"].as<std::string>();
}
void unpack_optionals(const YAML::Node &node, IO *io) {
  if (node["role"]) {
    unpack_role(node, io);
    if (io->role.has_value() && is_signal_role(io->role.value()) &&
        !node["unit"]) {
      throw std::runtime_error(
          std::format("IO '{}' with role '{}' must have a unit",
                      node["name"] ? node["name"].as<std::string>()
                                   : node["suffix"].as<std::string>(),
                      node["role"].as<std::string>()));
    }
  }
  if (node["unit"]) {
    unpack_unit(node, io);
  }
  if (node["precision"]) {
    unpack_precision(node, io);
  }
  if (node["format"]) {
    unpack_format(node, io);
  }
  if (node["min"]) {
    unpack_min(node, io);
  }
  if (node["max"]) {
    unpack_max(node, io);
  }
}

IO makeChannelGroupIO(const YAML::Node &node) {
  if (!node["type"]) {
    throw std::runtime_error("Channel group IO must have 'type'");
  }
  if (!node["name"] && !node["suffix"]) {
    throw std::runtime_error("Channel group IO must have 'name' or 'suffix'");
  }

  IO io;
  io.name = node["name"] ? node["name"].as<std::string>()
                         : node["suffix"].as<std::string>();
  io.type = mapType(node["type"].as<std::string>());
  unpack_optionals(node, &io);
  return io;
}
} // namespace

IO makeIO(const YAML::Node &node) {
  if (!node["name"] || !node["type"]) {
    throw std::runtime_error("IO must have 'name' and 'type'");
  }

  IO io;
  io.name = node["name"].as<std::string>();
  io.type = mapType(node["type"].as<std::string>());
  unpack_optionals(node, &io);
  return io;
}
IO makeNamelessIO(const YAML::Node &node) {
  if (!node["type"]) {
    throw std::runtime_error("IO must have 'type'");
  }
  IO io;
  io.type = mapType(node["type"].as<std::string>());
  unpack_optionals(node, &io);
  return io;
}

IO parseParam(const YAML::Node &node,
              const std::unordered_map<std::string, IO> &io_lookup) {
  // Reference case
  if (node["io"]) {
    auto ioName = node["io"].as<std::string>();

    auto it = io_lookup.find(ioName);
    if (it == io_lookup.end()) {
      throw std::runtime_error("Unknown IO in parameters: " + ioName);
    }

    return it->second;
  }

  // Inline case
  if (node["name"] && node["type"]) {
    return makeIO(node);
  }

  throw std::runtime_error("Invalid parameter format");
}

static ChannelGroupData load_channel_groups_impl(const YAML::Node &doc) {
  ChannelGroupData result;

  if (!doc["channel_groups"]) {
    return result;
  }

  YAML::Node groups = doc["channel_groups"];

  result.channel_groups.reserve(groups.size());
  result.channel_group_io_lookup.reserve(groups.size());

  for (const auto &group : groups) {
    const auto groupName = group["name"].as<std::string>();

    std::vector<IO> params;

    const auto &chParamNode = group["channel_parameter"];

    if (chParamNode.IsSequence()) {
      for (const auto &p : chParamNode) {
        IO io = makeNamelessIO(p);
        io.name = groupName;
        params.push_back(io);
      }
    } else {
      IO io = makeNamelessIO(chParamNode);
      io.name = groupName;
      params.push_back(io);
    }

    std::unordered_map<std::string, IO> group_io_lookup;

    if (group["io_types"]) {
      group_io_lookup.reserve(group["io_types"].size());

      for (const auto &ioTypeNode : group["io_types"]) {
        IO io = makeChannelGroupIO(ioTypeNode);
        group_io_lookup[io.name] = io;
      }
    }

    result.channel_groups.emplace(groupName, std::move(params));
    result.channel_group_io_lookup.emplace(groupName,
                                           std::move(group_io_lookup));
  }

  return result;
}
static std::unordered_map<std::string, IO> load_io_impl(const YAML::Node &doc) {
  std::unordered_map<std::string, IO> io_lookup;
  for (const auto &ioNode : doc["io"]) {
    IO io = makeIO(ioNode);
    io_lookup[io.name] = io;
  }
  return io_lookup;
}
ChannelGroupData load_channel_groups(const std::filesystem::path &api_path) {
  YAML::Node doc = YAML::LoadFile(api_path.string());
  return load_channel_groups_impl(doc);
}
std::unordered_map<std::string, IO>
load_io(const std::filesystem::path &api_path) {
  YAML::Node doc = YAML::LoadFile(api_path.string());
  return load_io_impl(doc);
}

std::unordered_map<std::string, Command>
load_api(const std::filesystem::path &api_path) {
  std::unordered_map<std::string, Command> instrument_commands;
  YAML::Node doc = YAML::LoadFile(api_path.string());
  if (!doc["protocol"]) {
    throw std::runtime_error("Missing required api field: protocol");
  }
  if (!doc["protocol"]["type"]) {
    throw std::runtime_error("Missing required api field: protocol type");
  }
  bool foundVisa = false;
  auto type = doc["protocol"]["type"].as<std::string>();
  if (type != "VISA") {
    if (!doc["protocol"]["name"]) {
      throw std::runtime_error("Missing required api field: protocol name");
    }
  } else {
    foundVisa = true;
  }

  // ---------------- IO LOOKUP ----------------
  if (!doc["io"]) {
    throw std::runtime_error("Missing required field: io");
  }
  std::unordered_map<std::string, IO> io_lookup = load_io_impl(doc);

  // ---------------- CHANNEL GROUPS ----------------
  auto channel_group_data = load_channel_groups_impl(doc);
  auto &channel_groups = channel_group_data.channel_groups;
  auto &channel_group_io_lookup = channel_group_data.channel_group_io_lookup;

  // ---------------- COMMANDS ----------------
  if (!doc["commands"]) {
    throw std::runtime_error("Missing required field: commands");
  }
  YAML::Node commands = doc["commands"];
  instrument_commands.reserve(commands.size());
  for (auto it = commands.begin(); it != commands.end(); ++it) {
    auto cmdName = it->first.as<std::string>();
    YAML::Node cmdNode = it->second;
    Command cmd;
    cmd.name = cmdName;
    std::optional<std::string> groupName;
    if (cmdNode["channel_group"]) {
      groupName = cmdNode["channel_group"].as<std::string>();
      if (!channel_groups.contains(*groupName)) {
        throw std::runtime_error("Unknown channel_group: " + *groupName);
      }
      cmd.group_name = groupName;
    }

    std::unordered_map<std::string, IO> scoped_io_lookup = io_lookup;
    if (groupName) {
      const auto group_io_it = channel_group_io_lookup.find(*groupName);
      if (group_io_it != channel_group_io_lookup.end()) {
        for (const auto &[name, io] : group_io_it->second) {
          scoped_io_lookup[name] = io;
        }
      }
    }

    // ---- reserve parameters ----
    size_t param_count = 0;
    if (groupName) {
      param_count += channel_groups.at(*groupName).size();
    }
    if (cmdNode["parameters"]) {
      param_count += cmdNode["parameters"].size();
    }
    cmd.parameters.reserve(param_count);
    // ---- reserve returns ----
    if (cmdNode["outputs"]) {
      cmd.returns.reserve(cmdNode["outputs"].size());
    }
    // ---- channel group injection FIRST ----
    if (groupName) {
      for (const auto &chParam : channel_groups.at(*groupName)) {
        cmd.parameters.push_back(chParam);
      }
    }

    // ---- normal parameters ----
    if (cmdNode["parameters"]) {
      for (const auto &param : cmdNode["parameters"]) {
        cmd.parameters.push_back(parseParam(param, scoped_io_lookup));
      }
    }

    // ---- outputs ----
    if (cmdNode["outputs"]) {
      for (const auto &out : cmdNode["outputs"]) {
        auto ioName = out.as<std::string>();
        if (!scoped_io_lookup.contains(ioName)) {
          throw std::runtime_error("Unknown IO in outputs: " + ioName);
        }
        cmd.returns.push_back(scoped_io_lookup.at(ioName));
      }
    }

    // ---- template ----
    if (cmdNode["template"]) {
      cmd.temp = cmdNode["template"].as<std::string>();
    }

    instrument_commands.emplace(cmd.name, std::move(cmd));
  }
  return instrument_commands;
}
namespace {

IOConfig
parse_io_config_entry(const std::string &io_name, const YAML::Node &node,
                      const std::unordered_set<std::string> &valid_ios) {
  if (!valid_ios.contains(io_name)) {
    throw std::runtime_error("Unknown IO in io_config: " + io_name);
  }

  if (!node.IsMap()) {
    throw std::runtime_error("io_config." + io_name + " must be an object");
  }

  static const std::unordered_set<std::string> allowed_fields{
      "transformed-unit", "offset", "scale"};

  for (const auto &field : node) {
    const auto key = field.first.as<std::string>();

    if (!allowed_fields.contains(key)) {
      throw std::runtime_error(std::format(
          "io_config.{} contains unknown field '{}'", io_name, key));
    }
  }

  IOConfig cfg;

  if (node["transformed-unit"]) {
    cfg.unit = node["transformed-unit"].as<std::string>();
  }

  if (node["offset"]) {
    cfg.offset = node["offset"].as<double>();
  } else {
    cfg.offset = 0.0;
  }

  if (node["scale"]) {
    cfg.scale = node["scale"].as<double>();
  } else {
    cfg.scale = 1.0;
  }

  return cfg;
}

} // namespace

InstrumentConfig load_config(const std::filesystem::path &config_path) {
  InstrumentConfig cfg;

  YAML::Node doc = YAML::LoadFile(config_path.string());

  // ---- required fields ----
  if (!doc["name"]) {
    throw std::runtime_error("Missing required field: name");
  }
  cfg.name = doc["name"].as<std::string>();

  if (!doc["api_ref"]) {
    throw std::runtime_error("Missing required field: api_ref");
  }
  cfg.api_ref = doc["api_ref"].as<std::string>();
  YAML::Node api = YAML::LoadFile(config_path.parent_path() /
                                  std::filesystem::path(cfg.api_ref));
  if (!api["protocol"]) {
    throw std::runtime_error("Missing required api field: protocol");
  }
  if (!api["protocol"]["type"]) {
    throw std::runtime_error("Missing required api field: protocol type");
  }

  auto type = api["protocol"]["type"].as<std::string>();
  if (type == "VISA") {
    cfg.api_type.type = VISA;
  } else {
    cfg.api_type.type = OTHER;
    if (!api["protocol"]["name"]) {
      throw std::runtime_error("Missing required api field: protocol name");
    }
    cfg.api_type.name = api["protocol"]["name"].as<std::string>();
  }

  // ---- optional connection block ----
  if (doc["connection"]) {
    const YAML::Node &conn = doc["connection"];

    if (conn["address"]) {
      cfg.address = conn["address"].as<std::string>();
    }
    if (conn["baudrate"]) {
      cfg.baudrate = conn["baudrate"].as<uint32_t>();
    }
    if (conn["custom"]) {
      cfg.custom = conn["custom"].as<std::string>();
    }
  }

  // ---- optional startup block ----
  if (doc["startup"]) {
    const YAML::Node &start = doc["startup"];

    if (start["delay_ms"]) {
      cfg.startup_delay = start["delay_ms"].as<uint32_t>();
    }
    if (start["init_commands"]) {
      const YAML::Node &init = start["init_commands"];

      if (init.IsSequence()) {
        if (!cfg.init_commands.has_value()) {
          cfg.init_commands = std::vector<std::string>();
        }
        for (const auto &cmd_node : init) {
          cfg.init_commands->push_back(cmd_node.as<std::string>());
        }
      } else {
        throw std::runtime_error("init_commands must be a YAML sequence/list");
      }
    }
  }

  std::unordered_set<std::string> valid_ios;
  for (const auto &ioNode : api["io"]) {
    IO io = makeIO(ioNode);

    if (io.role.has_value() && is_signal_role(io.role.value())) {
      valid_ios.insert(io.name);
    }
  }
  // Build defaults for every valid IO.
  for (const auto &io_name : valid_ios) {
    cfg.io_config.emplace(io_name, IOConfig{});
  }

  // ---- required io_config block ----
  if (!doc["io_config"]) {
    throw std::runtime_error("Missing required field: io_config");
  }

  const YAML::Node &io_config = doc["io_config"];

  if (io_config.IsNull()) {
    return cfg;
  }
  if (!io_config.IsMap()) {
    throw std::runtime_error("io_config must be a YAML mapping");
  }

  if (!api["io"]) {
    throw std::runtime_error("Missing required api field: io");
  }

  for (const auto &entry : io_config) {
    const auto io_name = entry.first.as<std::string>();
    const YAML::Node &cfg_node = entry.second;

    cfg.io_config.at(io_name) =
        parse_io_config_entry(io_name, cfg_node, valid_ios);
  }

  return cfg;
}
} // namespace instserver
