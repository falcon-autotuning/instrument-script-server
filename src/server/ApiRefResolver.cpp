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

  // If absolute, use directly (must exist)
  if (p.is_absolute()) {
    if (!std::filesystem::exists(p)) {
      throw std::runtime_error("API definition file not found: " + p.string());
    }
    return std::filesystem::canonical(p).string();
  }

  // p is relative. Try resolution rules (backwards-compatible):
  // 1) Prefer config parent directory (so colocated api files work)
  // 2) Fallback to current working directory (preserves previous behavior)
  std::filesystem::path cfg_parent =
      std::filesystem::path(config_path).parent_path();
  if (cfg_parent.empty()) {
    cfg_parent = std::filesystem::current_path();
  }

  std::filesystem::path attempt1 = cfg_parent / p;
  if (std::filesystem::exists(attempt1)) {
    return std::filesystem::canonical(attempt1).string();
  }

  std::filesystem::path attempt2 = std::filesystem::current_path() / p;
  if (std::filesystem::exists(attempt2)) {
    return std::filesystem::canonical(attempt2).string();
  }

  // Neither candidate exists — produce an informative error referencing the
  // preferred (config-relative) path to help debugging.
  throw std::runtime_error("API definition file not found: " +
                           attempt1.string());
}

} // namespace server
} // namespace instserver
