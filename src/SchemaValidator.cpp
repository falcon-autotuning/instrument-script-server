#include "instrument-server/SchemaValidator.hpp"

#include <regex>
#include <set>
#include <string>
#include <vector>
#include <yaml-cpp/yaml.h>

namespace instserver {

// Forward declarations for embedded schemas
extern const char *INSTRUMENT_API_SCHEMA;
extern const char *INSTRUMENT_CONFIGURATION_SCHEMA;

[[maybe_unused]] static std::string get_yaml_type(const YAML::Node &node) {
  if (node.IsScalar())
    return "scalar";
  if (node.IsSequence())
    return "sequence";
  if (node.IsMap())
    return "map";
  return "unknown";
}

static std::string node_path(const std::vector<std::string> &path) {
  std::string out;
  for (const auto &p : path) {
    out += "/" + p;
  }
  return out;
}

static void add_error(ValidationResult &result,
                      const std::vector<std::string> &path,
                      const std::string &msg) {
  result.valid = false;
  result.errors.push_back({node_path(path), msg, 0, 0});
}

[[maybe_unused]] static bool has_key(const YAML::Node &node,
                                     const std::string &key) {
  return node[key].IsDefined();
}

static void validate_io_against_channel_groups(const YAML::Node &io,
                                               const YAML::Node &channel_groups,
                                               ValidationResult &result,
                                               std::vector<std::string> path) {
  if (!channel_groups)
    return;
  std::set<std::string> io_names;
  for (const auto &entry : io) {
    io_names.insert(entry["name"].as<std::string>());
  }
  for (const auto &group : channel_groups) {
    std::string group_name = group["name"].as<std::string>();
    int min_ch = group["channel_parameter"]["min"].as<int>();
    int max_ch = group["channel_parameter"]["max"].as<int>();
    const auto &io_types = group["io_types"];
    for (int ch = min_ch; ch <= max_ch; ++ch) {
      for (const auto &io_type : io_types) {
        std::string expected_name = group_name + std::to_string(ch) + "_" +
                                    io_type["suffix"].as<std::string>();
        if (io_names.find(expected_name) == io_names.end()) {
          std::vector<std::string> err_path = path;
          err_path.push_back("io");
          add_error(result, err_path,
                    "Missing IO entry for channel group '" + group_name +
                        "', channel " + std::to_string(ch) + ", suffix '" +
                        io_type["suffix"].as<std::string>() +
                        "' (expected name: '" + expected_name + "')");
        }
      }
    }
  }
}

ValidationResult
SchemaValidator::validate_instrument_api(const std::string &yaml_path) {
  ValidationResult result;
  result.valid = true;
  try {
    YAML::Node doc = YAML::LoadFile(yaml_path);
    // Validate required top-level fields
    std::vector<std::string> path;
    for (const auto &key :
         {"api_version", "instrument", "protocol", "io", "commands"}) {
      if (!doc[key] || !doc[key].IsDefined()) {
        add_error(result, path,
                  std::string("Missing required field '") + key + "'");
      }
    }
    // Validate io is a sequence of maps with required fields
    if (!doc["io"] || !doc["io"].IsSequence()) {
      add_error(result, {"io"}, "IO must be a sequence");
    } else {
      for (size_t i = 0; i < doc["io"].size(); ++i) {
        const auto &entry = doc["io"][i];
        if (!entry.IsMap()) {
          add_error(result, {"io", std::to_string(i)},
                    "IO entry must be a map");
          continue;
        }
        for (const auto &req : {"name", "type", "role"}) {
          if (!entry[req] || !entry[req].IsDefined()) {
            add_error(result, {"io", std::to_string(i)},
                      std::string("Missing required IO field '") + req + "'");
          }
        }
      }
    }

    // Validate channel_groups and that io matches expanded template
    if (doc["channel_groups"]) {
      if (!doc["channel_groups"].IsSequence()) {
        add_error(result, {"channel_groups"},
                  "channel_groups must be a sequence");
      } else {
        validate_io_against_channel_groups(doc["io"], doc["channel_groups"],
                                           result, {});
        // Validate channel_group structure
        for (size_t g = 0; g < doc["channel_groups"].size(); ++g) {
          const auto &group = doc["channel_groups"][g];
          std::vector<std::string> group_path = {"channel_groups",
                                                 std::to_string(g)};
          for (const auto &req : {"name", "channel_parameter", "io_types"}) {
            if (!group[req] || !group[req].IsDefined()) {
              add_error(result, group_path,
                        std::string("Missing required channel_group field '") +
                            req + "'");
            }
          }
          if (group["channel_parameter"]) {
            for (const auto &req : {"name", "type", "min", "max"}) {
              if (!group["channel_parameter"][req] ||
                  !group["channel_parameter"][req].IsDefined()) {
                add_error(
                    result, group_path,
                    std::string("Missing required channel_parameter field '") +
                        req + "'");
              }
            }
            if (group["channel_parameter"]["type"] &&
                group["channel_parameter"]["type"].as<std::string>() != "int") {
              add_error(result, group_path,
                        "channel_parameter type must be 'int'");
            }
          }
          if (group["io_types"]) {
            if (!group["io_types"].IsSequence()) {
              add_error(result, group_path, "io_types must be a sequence");
            } else {
              for (size_t t = 0; t < group["io_types"].size(); ++t) {
                const auto &io_type = group["io_types"][t];
                std::vector<std::string> io_type_path = group_path;
                io_type_path.push_back("io_types");
                io_type_path.push_back(std::to_string(t));
                for (const auto &req : {"suffix", "type", "role"}) {
                  if (!io_type[req] || !io_type[req].IsDefined()) {
                    add_error(result, io_type_path,
                              std::string("Missing required io_type field '") +
                                  req + "'");
                  }
                }
              }
            }
          }
        }
      }
    }

    // Validate commands
    if (!doc["commands"] || !doc["commands"].IsMap()) {
      add_error(result, {"commands"}, "commands must be a map/object");
    } else {
      for (auto it = doc["commands"].begin(); it != doc["commands"].end();
           ++it) {
        std::string cmd_name = it->first.as<std::string>();
        const auto &cmd = it->second;
        std::vector<std::string> cmd_path = {"commands", cmd_name};
        // Required fields
        for (const auto &req : {"template", "parameters", "outputs"}) {
          if (!cmd[req] || !cmd[req].IsDefined()) {
            add_error(result, cmd_path,
                      std::string("Missing required command field '") + req +
                          "'");
          }
        }
        // Validate parameters
        if (cmd["parameters"] && !cmd["parameters"].IsSequence()) {
          add_error(result, cmd_path, "parameters must be a sequence");
          continue; // Prevent further processing of this command
        }
        // Validate outputs (can be empty)
        if (cmd["outputs"] && !cmd["outputs"].IsSequence()) {
          add_error(result, cmd_path, "outputs must be a sequence");
          continue; // Prevent further processing of this command
        }
        // If channel_group is set, outputs must be suffixes, else must be io
        // names
        if (!cmd["outputs"] || !cmd["outputs"].IsSequence()) {
          if (cmd["channel_group"]) {
            add_error(result, cmd_path,
                      "outputs must be a sequence of suffixes when "
                      "channel_group is set");
            continue;
          } else {
            add_error(result, cmd_path,
                      "outputs must be a sequence of io names when "
                      "channel_group is not set");
            continue;
          }
        }
        if (cmd["template"] && cmd["template"].IsScalar()) {
          std::string tmpl = cmd["template"].as<std::string>();
          std::set<std::string> allowed_names;

          // Add parameter names
          if (cmd["parameters"] && cmd["parameters"].IsSequence()) {
            for (const auto &param : cmd["parameters"]) {
              if (param["name"] && param["name"].IsScalar()) {
                allowed_names.insert(param["name"].as<std::string>());
              } else if (param["io"] && param["io"].IsScalar()) {
                allowed_names.insert(param["io"].as<std::string>());
              }
            }
          }

          // Add channel_group name if present
          // Re-access from doc to avoid iterator issues
          std::string channel_group_name;
          YAML::Node cmd_fresh = doc["commands"][cmd_name];
          if (cmd_fresh["channel_group"] &&
              cmd_fresh["channel_group"].IsDefined()) {
            if (!cmd_fresh["channel_group"].IsScalar()) {
              add_error(result, cmd_path,
                        "channel_group must be a scalar string if defined");
              continue;
            }
            channel_group_name = cmd_fresh["channel_group"].as<std::string>();
            allowed_names.insert(channel_group_name);
          }

          // Find all {xxxx} in the template
          std::regex brace_re("\\{([^}]+)\\}");
          auto words_begin =
              std::sregex_iterator(tmpl.begin(), tmpl.end(), brace_re);
          auto words_end = std::sregex_iterator();
          std::set<std::string> found_names;
          for (auto i = words_begin; i != words_end; ++i) {
            std::string name = (*i)[1].str();
            found_names.insert(name);
            if (allowed_names.find(name) == allowed_names.end()) {
              add_error(
                  result, cmd_path,
                  "Template placeholder {" + name +
                      "} does not match any parameter or channel_group name");
            }
          }

          // If channel_group is set, its name must appear in the template
          if (!channel_group_name.empty() &&
              found_names.find(channel_group_name) == found_names.end()) {
            add_error(result, cmd_path,
                      "Template for command with channel_group must include {" +
                          channel_group_name + "} placeholder");
          }
        }
      }
    }
  } catch (const YAML::Exception &e) {
    result.valid = false;
    result.errors.push_back(
        {"", std::string("YAML parse error: ") + e.what(), 0, 0});
  }
  return result;
}

std::string SchemaValidator::get_instrument_api_schema() {
  return INSTRUMENT_API_SCHEMA;
}
std::string SchemaValidator::get_instrument_configuration_schema() {
  return INSTRUMENT_CONFIGURATION_SCHEMA;
}

// Helper:  split semicolon-delimited string into vector
static std::vector<std::string> split_semicolon(const std::string &s) {
  std::vector<std::string> out;
  size_t start = 0, end;
  while ((end = s.find(';', start)) != std::string::npos) {
    out.push_back(s.substr(start, end - start));
    start = end + 1;
  }
  if (start < s.size())
    out.push_back(s.substr(start));
  return out;
}

ValidationResult
SchemaValidator::validate_quantum_dot_device(const std::string &yaml_path) {
  ValidationResult result;
  result.valid = true;
  try {
    YAML::Node doc = YAML::LoadFile(yaml_path);

    // Top-level required fields
    std::vector<std::string> path;
    for (const auto &key : {"global", "groups", "wiringDC"}) {
      if (!doc[key] || !doc[key].IsDefined()) {
        add_error(result, path,
                  std::string("Missing required field '") + key + "'");
      }
    }

    // Parse global gates
    std::map<std::string, std::vector<std::string>> global_gates;
    std::vector<std::string> all_global_gates;
    if (doc["global"]) {
      const auto &global = doc["global"];
      std::vector<std::string> gpath = {"global"};
      for (const auto &key : {"ScreeningGates", "PlungerGates", "Ohmics",
                              "BarrierGates", "ReservoirGates"}) {
        if (!global[key] || !global[key].IsDefined()) {
          add_error(result, gpath,
                    std::string("Missing required global field '") + key + "'");
        } else {
          global_gates[key] = split_semicolon(global[key].as<std::string>());
          all_global_gates.insert(all_global_gates.end(),
                                  global_gates[key].begin(),
                                  global_gates[key].end());
        }
      }
    }

    // Validate groups section and check all group gates are in global
    std::set<std::string> used_gates;
    if (doc["groups"]) {
      const auto &groups = doc["groups"];
      if (!groups.IsMap()) {
        add_error(result, {"groups"}, "groups must be a map/object");
      } else {
        for (auto it = groups.begin(); it != groups.end(); ++it) {
          std::string group_name = it->first.as<std::string>();
          const auto &group = it->second;
          std::vector<std::string> gpath = {"groups", group_name};
          for (const auto &key :
               {"Name", "NumDots", "ScreeningGates", "ReservoirGates",
                "PlungerGates", "BarrierGates", "Order"}) {
            if (!group[key] || !group[key].IsDefined()) {
              add_error(result, gpath,
                        std::string("Missing required group field '") + key +
                            "'");
            }
          }
          // Check group gates are in global
          for (const auto &gate_type : {"ScreeningGates", "ReservoirGates",
                                        "PlungerGates", "BarrierGates"}) {
            if (group[gate_type] && group[gate_type].IsScalar()) {
              auto group_gates =
                  split_semicolon(group[gate_type].as<std::string>());
              for (const auto &gate : group_gates) {
                used_gates.insert(gate);
                if (std::find(global_gates[gate_type].begin(),
                              global_gates[gate_type].end(),
                              gate) == global_gates[gate_type].end()) {
                  add_error(result, gpath,
                            std::string("Gate '") + gate + "' in " + gate_type +
                                " not found in global " + gate_type);
                }
              }
            }
          }
          // Validate Order
          if (group["Order"] && group["Order"].IsScalar()) {
            auto order = split_semicolon(group["Order"].as<std::string>());
            size_t n = order.size();
            if (n < 3) {
              add_error(result, gpath, "Order must have at least 3 entries");
            } else {
              // Ohmic at first and last
              if (std::find(global_gates["Ohmics"].begin(),
                            global_gates["Ohmics"].end(),
                            order.front()) == global_gates["Ohmics"].end()) {
                add_error(result, gpath,
                          "First entry in Order must be an Ohmic");
              }
              if (std::find(global_gates["Ohmics"].begin(),
                            global_gates["Ohmics"].end(),
                            order.back()) == global_gates["Ohmics"].end()) {
                add_error(result, gpath,
                          "Last entry in Order must be an Ohmic");
              }
              // Reservoir at first and second-to-last
              if (std::find(global_gates["ReservoirGates"].begin(),
                            global_gates["ReservoirGates"].end(),
                            order[1]) == global_gates["ReservoirGates"].end()) {
                add_error(result, gpath,
                          "Second entry in Order must be a ReservoirGate");
              }
              if (std::find(global_gates["ReservoirGates"].begin(),
                            global_gates["ReservoirGates"].end(),
                            order[n - 2]) ==
                  global_gates["ReservoirGates"].end()) {
                add_error(
                    result, gpath,
                    "Second-to-last entry in Order must be a ReservoirGate");
              }
              // Center:  barrier-plunger-barrier pattern
              int plunger_count = 0, barrier_count = 0;
              for (size_t i = 2; i + 2 < n; ++i) {
                if (i % 2 == 0) {
                  // Should be barrier
                  if (std::find(global_gates["BarrierGates"].begin(),
                                global_gates["BarrierGates"].end(), order[i]) ==
                      global_gates["BarrierGates"].end()) {
                    add_error(result, gpath,
                              "Order entry " + std::to_string(i) +
                                  " should be a BarrierGate");
                  }
                  barrier_count++;
                } else {
                  // Should be plunger
                  if (std::find(global_gates["PlungerGates"].begin(),
                                global_gates["PlungerGates"].end(), order[i]) ==
                      global_gates["PlungerGates"].end()) {
                    add_error(result, gpath,
                              "Order entry " + std::to_string(i) +
                                  " should be a PlungerGate");
                  }
                  plunger_count++;
                }
              }
              if (barrier_count != plunger_count + 1) {
                add_error(result, gpath,
                          "There must be exactly one more BarrierGate than "
                          "PlungerGate in the Order");
              }
            }
          }
        }
      }
    }

    // Check all global gates are used in groups
    for (const auto &[type, gates] : global_gates) {
      if (type == "Ohmics")
        continue;
      for (const auto &gate : gates) {
        if (used_gates.find(gate) == used_gates.end()) {
          add_error(result, {"global"},
                    "Global gate '" + gate + "' of type '" + type +
                        "' is not used in any group");
        }
      }
    }

    // Validate wiringDC:  must be empty or contain all global gates
    if (doc["wiringDC"]) {
      const auto &wiring = doc["wiringDC"];
      if (!wiring.IsMap()) {
        add_error(result, {"wiringDC"}, "wiringDC must be a map/object");
      } else if (wiring.size() != 0 &&
                 wiring.size() != all_global_gates.size()) {
        add_error(result, {"wiringDC"},
                  "wiringDC must be empty or contain an entry for every "
                  "global gate");
      }
      for (auto it = wiring.begin(); it != wiring.end(); ++it) {
        std::string conn_name = it->first.as<std::string>();
        const auto &conn = it->second;
        std::vector<std::string> wpath = {"wiringDC", conn_name};
        for (const auto &key : {"resistance", "capacitance"}) {
          if (!conn[key] || !conn[key].IsDefined()) {
            add_error(result, wpath,
                      std::string("Missing required wiringDC field '") + key +
                          "'");
          }
        }
        if (conn["resistance"] && conn["resistance"].IsScalar() &&
            conn["resistance"].as<double>() < 0) {
          add_error(result, wpath, "resistance must be >= 0");
        }
        if (conn["capacitance"] && conn["capacitance"].IsScalar() &&
            conn["capacitance"].as<double>() < 0) {
          add_error(result, wpath, "capacitance must be >= 0");
        }
      }
    }

  } catch (const YAML::Exception &e) {
    result.valid = false;
    result.errors.push_back(
        {"", std::string("YAML parse error: ") + e.what(), 0, 0});
  }
  return result;
}
ValidationResult SchemaValidator::validate_instrument_configuration(
    const std::string &yaml_path) {
  ValidationResult result;
  result.valid = true;
  try {
    YAML::Node doc = YAML::LoadFile(yaml_path);
    std::vector<std::string> path;

    // Required top-level fields
    for (const auto &key : {"name", "api_ref", "connection", "io_config"}) {
      if (!doc[key] || !doc[key].IsDefined()) {
        add_error(result, path,
                  std::string("Missing required field '") + key + "'");
      }
    }

    // Validate connection
    if (doc["connection"]) {
      if (!doc["connection"].IsMap()) {
        add_error(result, {"connection"}, "connection must be an object");
      } else if (!doc["connection"]["type"] ||
                 !doc["connection"]["type"].IsDefined()) {
        add_error(result, {"connection"},
                  "Missing required field 'type' in connection");
      }
    }

    // Validate io_config
    if (!doc["io_config"] || !doc["io_config"].IsMap()) {
      add_error(result, {"io_config"}, "io_config must be an object");
    } else {
      for (auto it = doc["io_config"].begin(); it != doc["io_config"].end();
           ++it) {
        std::string io_name = it->first.as<std::string>();
        const auto &io_entry = it->second;
        std::vector<std::string> io_path = {"io_config", io_name};
        // Required fields:  type, role
        for (const auto &req : {"type", "role"}) {
          if (!io_entry[req] || !io_entry[req].IsDefined()) {
            add_error(result, io_path,
                      std::string("Missing required IO field '") + req + "'");
          }
        }
        // type must be one of int, float, string, bool
        if (io_entry["type"] && io_entry["type"].IsScalar()) {
          std::string t = io_entry["type"].as<std::string>();
          if (t != "int" && t != "float" && t != "string" && t != "bool") {
            add_error(result, io_path,
                      "type must be one of:  int, float, string, bool");
          }
        }
        // role must be one of input, output, inout
        if (io_entry["role"] && io_entry["role"].IsScalar()) {
          std::string r = io_entry["role"].as<std::string>();
          if (r != "input" && r != "output" && r != "inout") {
            add_error(result, io_path,
                      "role must be one of: input, output, inout");
          }
        }
        // offset and scale must be numbers if present
        if (io_entry["offset"] && !io_entry["offset"].IsScalar()) {
          add_error(result, io_path, "offset must be a number");
        }
        if (io_entry["scale"] && !io_entry["scale"].IsScalar()) {
          add_error(result, io_path, "scale must be a number");
        }
      }
    }

  } catch (const YAML::Exception &e) {
    result.valid = false;
    result.errors.push_back(
        {"", std::string("YAML parse error: ") + e.what(), 0, 0});
  }
  return result;
}

} // namespace instserver
