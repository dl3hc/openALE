// logger.cpp — ConsoleLogger implementation for pal::ILogger

#include "PAL/logger.h"
#include <cstdio>
#include <cstdlib>
#include <ctime>
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

// Fixed filename, same "just works, no config" convention as station.state/
// lqa.bin — opened relative to the process CWD.
constexpr const char* kLogFileName = "openALE.log";
constexpr const char* kLogFileOld  = "openALE.log.old";
constexpr long        kMaxLogBytes = 5 * 1024 * 1024;  // rotate past ~5 MB

FILE* fopen_portable(const char* path, const char* mode) {
#ifdef _WIN32
    FILE* f = nullptr;
    fopen_s(&f, path, mode);
    return f;
#else
    return std::fopen(path, mode);
#endif
}

// If the log exceeds kMaxLogBytes, rotate before opening — otherwise long
// sessions grow it unbounded. Best-effort: a failed rotation (e.g. .old
// locked by another viewer) must not block logging.
void rotate_if_large() {
    FILE* probe = fopen_portable(kLogFileName, "rb");
    if (!probe) return;
    std::fseek(probe, 0, SEEK_END);
    const long size = std::ftell(probe);
    std::fclose(probe);
    if (size < kMaxLogBytes) return;
    std::remove(kLogFileOld);
    std::rename(kLogFileName, kLogFileOld);
}

std::string timestamp_now() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buf;
}

class ConsoleLogger : public ILogger {
public:
    ConsoleLogger() {
        rotate_if_large();
        // append mode: never truncate — a crash right after this run needs
        // the previous run's tail on disk after restart.
        log_file_ = fopen_portable(kLogFileName, "a");
    }
    ~ConsoleLogger() override {
        if (log_file_) std::fclose(log_file_);
    }

    void log(LogLevel level, const char* module, const char* message) override {
        if (level < min_level_) return;
        FILE* out = (level >= LogLevel::WARN) ? stderr : stdout;
        std::fprintf(out, "[%s][%s] %s\n", level_prefix(level), module, message);
        if (log_file_) {
            std::fprintf(log_file_, "%s [%s][%s] %s\n",
                         timestamp_now().c_str(), level_prefix(level), module, message);
            std::fflush(log_file_);  // durability over throughput — see file doc comment
        }
    }
    void set_level(LogLevel l) override { min_level_ = l; }
    LogLevel get_level() const override { return min_level_; }

private:
    LogLevel min_level_ = LogLevel::INFO;
    FILE*    log_file_  = nullptr;
};

} // anonymous namespace

static std::unique_ptr<ILogger> g_logger;

std::unique_ptr<ILogger> create_logger() { return std::make_unique<ConsoleLogger>(); }
ILogger* get_logger()                     { return g_logger.get(); }
void set_logger(std::unique_ptr<ILogger> logger) { g_logger = std::move(logger); }

} // namespace pal
