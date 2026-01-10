/*
    JamWide Plugin - logging.h
    Debug logging utilities
    
    Enable verbose logging with JAMWIDE_DEV_BUILD CMake option.
    Log file: /tmp/jamwide.log
*/

#ifndef JAMWIDE_LOGGING_H
#define JAMWIDE_LOGGING_H

#include <cstdio>
#include <cstdarg>
#include <cstdlib>

namespace jamwide {
namespace logging {

inline FILE* get_log_file() {
    static FILE* f = nullptr;
    if (!f) {
        const char* paths[2] = { "/tmp/jamwide.log", nullptr };
        char home_path[512];
        const char* home = std::getenv("HOME");
        if (home && *home) {
            std::snprintf(home_path, sizeof(home_path),
                          "%s/Library/Logs/jamwide.log", home);
            paths[1] = home_path;
        }
        for (int i = 0; i < 2 && !f; ++i) {
            if (!paths[i]) continue;
            f = std::fopen(paths[i], "a");
        }
        if (f) {
            setvbuf(f, nullptr, _IOLBF, 0);  // Line buffered
        }
    }
    return f;
}

inline void log_write(const char* fmt, ...) {
    FILE* f = get_log_file();
    if (!f) return;
    
    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);
    fflush(f);
}

inline void log_session_start() {
    FILE* f = get_log_file();
    if (f) {
        fprintf(f, "\n=== JamWide Session Started ===\n");
        fflush(f);
    }
}

} // namespace logging
} // namespace jamwide

// Main logging macro - always available for important messages
#define NLOG(...) jamwide::logging::log_write(__VA_ARGS__)

// Verbose logging - only in dev builds
#ifdef JAMWIDE_DEV_BUILD
    #define NLOG_VERBOSE(...) jamwide::logging::log_write(__VA_ARGS__)
#else
    #define NLOG_VERBOSE(...) ((void)0)
#endif

#endif // JAMWIDE_LOGGING_H
