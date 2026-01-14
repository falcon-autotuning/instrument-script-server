#pragma once

#include "instrument-server/export.h"
#include <string>

namespace instserver {
namespace server {

/// Resolve an api_ref (from an instrument configuration) into an absolute
/// filesystem path. Supports:
///  - absolute paths
///  - relative paths: resolved relative to the instrument configuration file
///    location (config_path's parent directory)
///  - file:// URIs (strips scheme and resolves remainder)
///
/// Throws std::runtime_error if the resolved file does not exist.
///
/// The function is exported from the instrument-server-core library so tests
/// and other executables can link against it on Windows.
INSTRUMENT_SERVER_API std::string
resolve_api_ref(const std::string &api_ref, const std::string &config_path);

} // namespace server
} // namespace instserver
