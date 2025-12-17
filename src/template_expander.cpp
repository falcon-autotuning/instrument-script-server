#include <fstream>
#include <iostream>
#include <string>
#include <unordered_set>
#include <yaml-cpp/yaml.h>

void expand_channel_groups(YAML::Node &root) {
  auto io = root["io"];
  if (!io)
    io = YAML::Node(YAML::NodeType::Sequence);
  if (!root["channel_groups"])
    return;
  for (const auto &group : root["channel_groups"]) {
    std::string group_name = group["name"].as<std::string>();
    int min_ch = group["channel_parameter"]["min"].as<int>();
    int max_ch = group["channel_parameter"]["max"].as<int>();
    const auto &io_types = group["io_types"];
    for (int ch = min_ch; ch <= max_ch; ++ch) {
      for (const auto &io_type : io_types) {
        YAML::Node io_entry;
        io_entry["name"] = group_name + std::to_string(ch) + "_" +
                           io_type["suffix"].as<std::string>();
        io_entry["type"] = io_type["type"].as<std::string>();
        io_entry["role"] = io_type["role"].as<std::string>();
        if (io_type["description"])
          io_entry["description"] = io_type["description"].as<std::string>();
        if (io_type["unit"])
          io_entry["unit"] = io_type["unit"].as<std::string>();
        root["io"].push_back(io_entry);
      }
    }
  }
}

void deduplicate_io(YAML::Node &root) {
  if (!root["io"] || !root["io"].IsSequence())
    return;
  YAML::Node unique_io(YAML::NodeType::Sequence);
  std::unordered_set<std::string> seen;
  for (const auto &entry : root["io"]) {
    std::string name = entry["name"].as<std::string>();
    if (seen.count(name) == 0) {
      unique_io.push_back(entry);
      seen.insert(name);
    }
  }
  root["io"] = unique_io;
}

int main(int argc, char *argv[]) {
  if (argc != 3) {
    std::cerr << "Usage: " << argv[0] << " <input.yaml> <output.yaml>\n";
    return 1;
  }
  YAML::Node root = YAML::LoadFile(argv[1]);
  expand_channel_groups(root);
  deduplicate_io(root);
  std::ofstream fout(argv[2]);
  fout << root;
  return 0;
}
