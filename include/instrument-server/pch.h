#pragma once
// Project-wide precompiled header for instrument-server
// Keep this header stable (only include headers that rarely change)
//
// Notes:
// - CMake's target_precompile_headers will use this file if it exists.
// - Add only headers that are expensive to parse and change infrequently.

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

// Common third-party headers used by the project.
// If you don't use all these in every TU, you can remove ones you
// don't need. These are the usual suspects for your repo.
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <yaml-cpp/yaml.h>

// sol2 is header-only and can be expensive; include if used widely.
// If your builds are failing because sol2 is optional, remove this
// or guard with an #ifdef that your cmake defines when sol2 is present.
// #include <sol/sol.hpp>

// If you have any internal project "small" headers needed everywhere,
// you can include them here (careful: changing these will invalidate PCH)
#include <instrument-server/export.h>

// End of pch.h
