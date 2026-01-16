#pragma once

// Centralized Windows socket / compatibility header.
// Always include this before <windows.h> in any translation unit that uses
// sockets to avoid the winsock.h vs winsock2.h conflict and to consistently
// link Ws2_32.

#ifdef _WIN32
// Reduce windows.h surface so it is less likely to pull in old winsock.h
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

// winsock2 must be included before windows.h. Include ws2tcpip for inet_pton,
// etc.
#include <winsock2.h>
#include <ws2tcpip.h>

// For MSVC auto-linking of the Winsock library
#if defined(_MSC_VER)
#pragma comment(lib, "Ws2_32.lib")
#endif

// Some code expects socklen_t on Windows; provide a consistent alias.
using socklen_t = int;

#endif // _WIN32
