#include <cstdlib>
#include <string>

#include "instrument-script-server/server/InstrumentCommand.hpp"
constexpr int DEFAULT_TIMEOUT_SECONDS = 5;
int g_measurement_timeout_sec = []() {
  const char *env = std::getenv("MEASUREMENT_TIMEOUT_SEC");

  if (env != nullptr) {
    try {
      int val = std::stoi(env);
      if (val > 0) {
        return val;
      }
    } catch (...) {
      // ignore invalid values
    }
  }

  return DEFAULT_TIMEOUT_SECONDS;
}();
