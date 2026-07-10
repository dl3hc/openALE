// logger.cpp — ConsoleLogger implementation for pal::ILogger

#include "PAL/logger.h"
#include <cstdio>
#include <memory>

namespace pal {

namespace {

static const char* level_prefix(LogLevel l) {
    switch (l) {
        case LogLevel::TRACE: return "TRACE";
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO ";
        case LogLevel::WARN:  return "WARN ";
        case LogLevel::ERROR: return "ERROR";
        case LogLevel::FATAL: return "FATAL";
        default:              return "?    ";
    }
}

class ConsoleLogger : public ILogger {
public:
    void log(LogLevel level, const char* module, const char* message) override {
        if (level < min_level_) return;
        FILE* out = (level >= LogLevel::WARN) ? stderr : stdout;
        std::fprintf(out, "[%s][%s] %s\n", level_prefix(level), module, message);
    }
    void set_level(LogLevel l) override { min_level_ = l; }
    LogLevel get_level() const override { return min_level_; }

private:
    LogLevel min_level_ = LogLevel::INFO;
};

} // anonymous namespace

static std::unique_ptr<ILogger> g_logger;

std::unique_ptr<ILogger> create_logger() { return std::make_unique<ConsoleLogger>(); }
ILogger* get_logger()                     { return g_logger.get(); }
void set_logger(std::unique_ptr<ILogger> logger) { g_logger = std::move(logger); }

} // namespace pal
