#pragma once

// Platform-specific lightweight aliases for process id / handle.
//
// On POSIX, ProcessId and ProcessHandle can both be pid_t (integer).
// On Windows, ProcessId is a numeric id (DWORD) whereas ProcessHandle is a
// HANDLE.

#if defined(_WIN32) || defined(_WIN64)
#include "instrument-server/compat/WinSock.hpp"
#include <windows.h>
using ProcessId = DWORD;
using ProcessHandle = HANDLE;
constexpr ProcessHandle InvalidProcessHandle = nullptr;
#else
#include <sys/types.h>
using ProcessId = pid_t;
// On POSIX we can use the pid as the "handle".
using ProcessHandle = pid_t;
constexpr ProcessHandle InvalidProcessHandle = static_cast<ProcessHandle>(-1);
#endif
