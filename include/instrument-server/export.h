#ifndef INSTRUMENT_SERVER_EXPORT_H
#define INSTRUMENT_SERVER_EXPORT_H

#ifdef _WIN32
#ifdef instrument_server_core_EXPORTS
#define INSTRUMENT_SERVER_API __declspec(dllexport)
#else
#define INSTRUMENT_SERVER_API __declspec(dllimport)
#endif
#ifdef INSTRUMENT_PLUGIN_EXPORTS
#define INSTRUMENT_PLUGIN_API __declspec(dllexport)
#else
#define INSTRUMENT_PLUGIN_API
#endif
#else
#define INSTRUMENT_SERVER_API
#define INSTRUMENT_PLUGIN_API
#endif

#endif // INSTRUMENT_SERVER_EXPORT_H
