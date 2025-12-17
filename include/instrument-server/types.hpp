#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace instserver {

struct Parameter {
  std::string name;
  std::string type; // "int", "float", "string", "bool"
  std::optional<std::string> description;
  std::optional<double> precision;
  std::optional<double> min;
  std::optional<double> max;
  std::optional<std::string> default_value;
  std::optional<std::string> unit;
};

struct Command {
  std::string name;
  std::string template_str;
  std::optional<std::string> description;
  std::vector<Parameter> parameters;
  std::string returns; // "void", "int", "float", "array<float>", etc.
  bool query;
};

struct ProtocolConfig {
  std::string type; // "VISA", "TCP", "Serial", etc.
  std::map<std::string, std::string> config;
};

struct InstrumentMetadata {
  std::string vendor;
  std::string model;
  std::string identifier; // e.g., "GPI1"
  std::optional<std::string> description;
  std::optional<std::string> firmware_version;
};

struct InstrumentAPI {
  std::string api_version;
  InstrumentMetadata instrument;
  ProtocolConfig protocol;
  std::map<std::string, Command> commands;
};

struct ContextField {
  std::string name;
  std::string type;
  std::optional<std::string> description;
  bool optional;
};

struct RuntimeContext {
  std::string name;
  std::string description;
  std::vector<ContextField> fields;
};

} // namespace instserver
