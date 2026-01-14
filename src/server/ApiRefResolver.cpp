#include "instrument-server/server/ApiRefResolver.hpp"

#include <filesystem>
#include <stdexcept>
#include <string>

namespace instserver {
namespace server {

static std::string strip_file_scheme(const std::string &s) {
  const std::string file_scheme = "file://";
  if (s.rfind(file_scheme, 0) == 0) {
    // Remove leading "file://"
    std::string out = s.substr(file_scheme.size());
#ifdef _WIN32
    // On Windows, "file:///C:/path..." is common; remove leading slash if
    // present and followed by drive letter.
    if (out.size() >= 3 && out[0] == '/' && out[2] == ':') {
      out.erase(0, 1);
    }
#endif
    return out;
  }
  return s;
}

std::string resolve_api_ref(const std::string &api_ref,
                            const std::string &config_path) {
  if (api_ref.empty()) {
    throw std::runtime_error("Empty api_ref");
  }

  // Handle file:// scheme
  std::string candidate = strip_file_scheme(api_ref);

  std::filesystem::path p(candidate);

  // If still empty (e.g., original was "file://"), error out
  if (p.empty()) {
    throw std::runtime_error("Invalid api_ref: '" + api_ref + "'");
  }

  // If relative, resolve relative to the instrument configuration file's parent
  // dir
  if (p.is_relative()) {
    std::filesystem::path cfg_parent =
        std::filesystem::path(config_path).parent_path();
    if (cfg_parent.empty()) {
      // If config_path had no parent (unlikely), use current path as fallback
      cfg_parent = std::filesystem::current_path();
    }
    p = cfg_parent / p;
  }

  // Make absolute (does not require existence)
  p = std::filesystem::absolute(p);

  // Existence check
  if (!std::filesystem::exists(p)) {
    throw std::runtime_error("API definition file not found: " + p.string());
  }

  return p.string();
}

} // namespace server
} // namespace instserver
