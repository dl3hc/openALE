/**
 * @file logger.h
 * @brief Platform-agnostic logging interface
 *
 * @author Alex Pennington, AAM402/KY4OLB
 * @date December 2024
 * @license MIT
 */

#pragma once

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>

// Windows.h (via winerror.h) and some Hamlib headers define ERROR, DEBUG,
// TRACE, FATAL as plain macros.  Save and clear them for the duration of this
// header so enum class members and inline function bodies compile cleanly.
// They are restored at the bottom of the file.
#pragma push_macro("ERROR")
#pragma push_macro("DEBUG")
#pragma push_macro("TRACE")
#pragma push_macro("FATAL")
#pragma push_macro("INFO")
#pragma push_macro("WARN")
#undef ERROR
#undef DEBUG
#undef TRACE
#undef FATAL
#undef INFO
#undef WARN

namespace pal {

/**
 * @brief Log severity levels
 */
enum class LogLevel {
    TRACE = 0,   ///< Detailed tracing
    DEBUG = 1,   ///< Debug information
    INFO = 2,    ///< General information
    WARN = 3,    ///< Warnings
    ERROR = 4,   ///< Errors
    FATAL = 5    ///< Fatal errors
};

/**
 * @brief Logger interface
 */
class ILogger {
public:
    virtual ~ILogger() = default;

    virtual void log(LogLevel level, const char* module, const char* message) = 0;

    virtual void set_level(LogLevel min_level) = 0;
    virtual LogLevel get_level() const = 0;

    // Convenience methods
    void trace(const char* module, const char* msg) { log(LogLevel::TRACE, module, msg); }
    void debug(const char* module, const char* msg) { log(LogLevel::DEBUG, module, msg); }
    void info(const char* module, const char* msg)  { log(LogLevel::INFO, module, msg); }
    void warn(const char* module, const char* msg)  { log(LogLevel::WARN, module, msg); }
    void error(const char* module, const char* msg) { log(LogLevel::ERROR, module, msg); }
    void fatal(const char* module, const char* msg) { log(LogLevel::FATAL, module, msg); }
};

/**
 * @brief Factory function - implemented per platform
 */
std::unique_ptr<ILogger> create_logger();

/**
 * @brief Global logger instance
 */
ILogger* get_logger();
void set_logger(std::unique_ptr<ILogger> logger);

// ── printf-style free functions ───────────────────────────────────────────────
// Use these at call sites instead of fprintf/printf.

inline void vlogf_(LogLevel level, const char* module, const char* fmt, va_list ap) {
    auto* logger = get_logger();
    if (!logger || level < logger->get_level()) return;
    char buf[512];
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    logger->log(level, module, buf);
}

inline void log_trace(const char* module, const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); vlogf_(LogLevel::TRACE, module, fmt, ap); va_end(ap);
}
inline void log_debug(const char* module, const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); vlogf_(LogLevel::DEBUG, module, fmt, ap); va_end(ap);
}
inline void log_info(const char* module, const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); vlogf_(LogLevel::INFO, module, fmt, ap); va_end(ap);
}
inline void log_warn(const char* module, const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); vlogf_(LogLevel::WARN, module, fmt, ap); va_end(ap);
}
inline void log_error(const char* module, const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); vlogf_(LogLevel::ERROR, module, fmt, ap); va_end(ap);
}

} // namespace pal

// Restore any macros that were pushed above.
#pragma pop_macro("WARN")
#pragma pop_macro("INFO")
#pragma pop_macro("FATAL")
#pragma pop_macro("TRACE")
#pragma pop_macro("DEBUG")
#pragma pop_macro("ERROR")
