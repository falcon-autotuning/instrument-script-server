#pragma once
#include <string>

namespace instserver {

// Legacy constants for backward compatibility
constexpr const char *VERSION = "1.1.0";
constexpr const char *SCHEMA_VERSION = "1.0.0";

/**
 * Get the version string for the instrument server
 * @return Version string in format "x.y.z" or "x.y.z-tag"
 */
std::string get_version();

/**
 * Get the Git commit hash if available
 * @return Git commit hash or "unknown" if not available
 */
std::string get_git_commit();

/**
 * Get the full version string including commit info
 * @return Full version string with git info if available
 */
std::string get_full_version();

} // namespace instserver
