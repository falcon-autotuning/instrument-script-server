#include "instrument-script-server/server/ParsingTools.hpp"
#include <yaml-cpp/yaml.h>
namespace instserver {
IO makeIO(const YAML::Node &node) {
  if (!node["name"] || !node["type"]) {
    throw std::runtime_error("IO must have 'name' and 'type'");
  }

  IO io;
  io.name = node["name"].as<std::string>();
  io.type = mapType(node["type"].as<std::string>());

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

std::unordered_map<std::string, Command>
load_api(const std::filesystem::path &api_path) {
  std::unordered_map<std::string, Command> instrument_commands;
  YAML::Node doc = YAML::LoadFile(api_path.string());

  // ---------------- IO LOOKUP ----------------
  if (!doc["io"]) {
    throw std::runtime_error("Missing required field: io");
  }

  std::unordered_map<std::string, IO> io_lookup;
  for (const auto &ioNode : doc["io"]) {
    IO io = makeIO(ioNode);
    io_lookup[io.name] = io;
  }

  // ---------------- CHANNEL GROUPS ----------------
  std::unordered_map<std::string, std::vector<IO>> channel_groups;
  if (doc["channel_groups"]) {
    YAML::Node groups = doc["channel_groups"];
    channel_groups.reserve(groups.size());
    for (const auto &group : groups) {
      auto groupName = group["name"].as<std::string>();
      std::vector<IO> params;
      const auto &chParamNode = group["channel_parameter"];
      if (chParamNode.IsSequence()) {
        // multiple params
        for (const auto &p : chParamNode) {
          params.push_back(makeIO(p));
        }
      } else {
        // single param
        params.push_back(makeIO(chParamNode));
      }
      channel_groups.emplace(groupName, std::move(params));
    }
  }
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
    // ---- reserve parameters ----
    size_t param_count = 0;
    if (cmdNode["channel_group"]) {
      auto groupName = cmdNode["channel_group"].as<std::string>();
      param_count += channel_groups[groupName].size();
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
    if (cmdNode["channel_group"]) {
      auto groupName = cmdNode["channel_group"].as<std::string>();
      if (channel_groups.count(groupName) == 0U) {
        throw std::runtime_error("Unknown channel_group: " + groupName);
      }
      for (const auto &chParam : channel_groups[groupName]) {
        cmd.parameters.push_back(chParam);
      }
    }
    // ---- normal parameters ----
    if (cmdNode["parameters"]) {
      for (const auto &param : cmdNode["parameters"]) {
        cmd.parameters.push_back(parseParam(param, io_lookup));
      }
    }
    // ---- outputs ----
    if (cmdNode["outputs"]) {
      for (const auto &out : cmdNode["outputs"]) {
        auto ioName = out.as<std::string>();
        if (io_lookup.count(ioName) == 0U) {
          throw std::runtime_error("Unknown IO in outputs: " + ioName);
        }
        cmd.returns.push_back(io_lookup.at(ioName));
      }
    }

    instrument_commands.emplace(cmd.name, std::move(cmd));
  }
  return instrument_commands;
}

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

  // ---- optional connection block ----
  if (doc["connection"]) {
    const YAML::Node &conn = doc["connection"];

    if (conn["address"]) {
      cfg.address = conn["address"].as<std::string>();
    }
    if (conn["baudrate"]) {
      cfg.baudrate = conn["baudrate"].as<int>();
    }
    if (conn["custom"]) {
      cfg.custom = conn["custom"].as<std::string>();
    }
  }

  return cfg;
}
} // namespace instserver
