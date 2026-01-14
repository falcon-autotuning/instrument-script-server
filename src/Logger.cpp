#include "instrument-server/Logger.hpp"

namespace instserver {

// DLL-safe singleton implementation
InstrumentLogger &InstrumentLogger::instance() {
  static InstrumentLogger logger;
  return logger;
}

} // namespace instserver
