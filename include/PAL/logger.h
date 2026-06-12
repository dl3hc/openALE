/**
 * @file logger.h
 * @brief Platform-agnostic logging interface for PC-ALE
 * 
 * @author Alex Pennington, AAM402/KY4OLB
 * @date December 2024
 * @license MIT
 */

#pragma once

#include <cstdint>
#include <memory>
#include <string>

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

} // namespace pal
