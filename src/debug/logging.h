/*
    NINJAM CLAP Plugin - logging.h
    Debug logging utilities
    
    Enable verbose logging with NINJAM_CLAP_DEV_BUILD CMake option.
    Log file: /tmp/ninjam-clap.log
*/

#ifndef NINJAM_LOGGING_H
#define NINJAM_LOGGING_H

#include <cstdio>
#include <cstdarg>
#include <cstdlib>

#ifdef _WIN32
#include "../../wdl/win32_utf8.h"
#endif

namespace ninjam {
namespace logging {

inline FILE* get_log_file() {
    static FILE* f = nullptr;
    if (!f) {
        const char* paths[2] = { "/tmp/ninjam-clap.log", nullptr };
        char home_path[512];
        const char* home = std::getenv("HOME");
        if (home && *home) {
            std::snprintf(home_path, sizeof(home_path),
                          "%s/Library/Logs/ninjam-clap.log", home);
            paths[1] = home_path;
        }
        for (int i = 0; i < 2 && !f; ++i) {
            if (!paths[i]) continue;
#ifdef _WIN32
            f = fopen(paths[i], "a");  // WDL redefines fopen to fopenUTF8 on Windows
#else
            f = std::fopen(paths[i], "a");
#endif
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
        fprintf(f, "\n=== NINJAM CLAP Session Started ===\n");
        fflush(f);
    }
}

} // namespace logging
} // namespace ninjam

// Main logging macro - always available for important messages
#define NLOG(...) ninjam::logging::log_write(__VA_ARGS__)

// Verbose logging - only in dev builds
#ifdef NINJAM_DEV_BUILD
    #define NLOG_VERBOSE(...) ninjam::logging::log_write(__VA_ARGS__)
#else
    #define NLOG_VERBOSE(...) ((void)0)
#endif

#endif // NINJAM_LOGGING_H
