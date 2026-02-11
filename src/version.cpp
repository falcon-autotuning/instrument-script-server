#include <instrument-server/version.hpp>

namespace instserver {

// These values will be replaced by CMake during build
#ifndef INSTSERVER_VERSION
#define INSTSERVER_VERSION "unknown"
#endif

#ifndef INSTSERVER_GIT_COMMIT
#define INSTSERVER_GIT_COMMIT "unknown"
#endif

#ifndef INSTSERVER_GIT_TAG
#define INSTSERVER_GIT_TAG ""
#endif

INSTRUMENT_SERVER_API std::string get_version() {
    std::string version = INSTSERVER_VERSION;
    std::string tag = INSTSERVER_GIT_TAG;
    
    // If we have a git tag and it's different from the version, append it
    if (!tag.empty() && tag != version && tag != std::string("v") + version) {
        version += "-" + tag;
    }
    
    return version;
}

INSTRUMENT_SERVER_API std::string get_git_commit() {
    return INSTSERVER_GIT_COMMIT;
}

INSTRUMENT_SERVER_API std::string get_full_version() {
    std::string version = get_version();
    std::string commit = get_git_commit();
    
    if (commit != "unknown" && !commit.empty()) {
        version += " (commit " + commit.substr(0, 7) + ")";
    }
    
    return version;
}

} // namespace instserver
