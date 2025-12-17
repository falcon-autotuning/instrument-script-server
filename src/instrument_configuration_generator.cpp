#include <fstream>
#include <iostream>
#include <string>
#include <yaml-cpp/yaml.h>

void generate_instrument_configuration(const std::string &api_yaml_path,
                                       const std::string &config_yaml_path) {
  YAML::Node api = YAML::LoadFile(api_yaml_path);

  YAML::Node config;
  config["name"] = "INSTRUMENT_NAME";
  config["api_ref"] = api_yaml_path;
  config["connection"]["type"] = "VISA"; // User should customize
  config["connection"]["address"] =
      "TCPIP::192.168.0.1::INSTR::0"; // User should customize

  YAML::Node io_config;
  for (const auto &io : api["io"]) {
    std::string role = io["role"].as<std::string>();
    if (role == "input" || role == "output" || role == "inout") {
      std::string name = io["name"].as<std::string>();
      YAML::Node io_entry;
      io_entry["type"] = io["type"].as<std::string>();
      io_entry["role"] = role;
      if (io["unit"])
        io_entry["unit"] = io["unit"].as<std::string>();
      io_entry["offset"] = 0;
      io_entry["scale"] = 1;
      io_config[name] = io_entry;
    }
  }
  config["io_config"] = io_config;

  std::ofstream fout(config_yaml_path);
  fout << config;
  fout.close();
}

int main(int argc, char *argv[]) {
  if (argc != 3) {
    std::cerr << "Usage: " << argv[0]
              << " <instrument-api.yaml> <instrument_configuration.yaml>\n";
    return 1;
  }
  generate_instrument_configuration(argv[1], argv[2]);
  std::cout << "Generated instrument configuration: " << argv[2] << std::endl;
  return 0;
}
