// crash_handler.cpp — writes crash.log on unhandled exception/fault.
//
// Uses raw fopen/fprintf, not pal::get_logger(): runs when process state is
// undefined (corrupted heap, exhausted stack, mid-unwind), so must depend on
// as little of the program as possible — same rationale as logger.cpp in
// .claude/CLAUDE.md's PAL printf exception list.

#include "PAL/crash_handler.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <ctime>
#include <exception>
#include <string>

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#include <malloc.h>   // _resetstkoflw
#pragma comment(lib, "dbghelp.lib")
#else
#include <execinfo.h> // backtrace / backtrace_symbols (glibc + macOS libc)
#include <unistd.h>   // readlink — self_exe_path() via /proc/self/exe
#endif

namespace pal {

namespace {

constexpr const char* kCrashLogFile = "crash.log";

FILE* fopen_portable(const char* path, const char* mode) {
#ifdef _WIN32
    FILE* f = nullptr;
    fopen_s(&f, path, mode);
    return f;
#else
    return std::fopen(path, mode);
#endif
}

void write_timestamp(FILE* f) {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::fprintf(f, "%04d-%02d-%02d %02d:%02d:%02d",
                 tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                 tm.tm_hour, tm.tm_min, tm.tm_sec);
}

#ifdef _WIN32
// Best-effort: walks call stack, resolves symbols via dbghelp if PDB available
// (Debug builds). SymInitialize() cheap enough to call fresh per crash
// record — at most one per process lifetime in practice.
void write_stack_trace(FILE* f) {
    void*  frames[64];
    HANDLE process = GetCurrentProcess();
    const bool syms_ok = SymInitialize(process, nullptr, TRUE) != FALSE;
    if (syms_ok) SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);

    const USHORT count = CaptureStackBackTrace(0, 64, frames, nullptr);
    for (USHORT i = 0; i < count; ++i) {
        const DWORD64 addr = reinterpret_cast<DWORD64>(frames[i]);
        std::fprintf(f, "  #%2u 0x%016llX", i, static_cast<unsigned long long>(addr));

        if (syms_ok) {
            alignas(SYMBOL_INFO) char sym_buf[sizeof(SYMBOL_INFO) + 256];
            auto* sym = reinterpret_cast<SYMBOL_INFO*>(sym_buf);
            sym->SizeOfStruct = sizeof(SYMBOL_INFO);
            sym->MaxNameLen   = 255;
            DWORD64 disp = 0;
            if (SymFromAddr(process, addr, &disp, sym)) {
                std::fprintf(f, " %s+0x%llX", sym->Name, static_cast<unsigned long long>(disp));
                DWORD line_disp = 0;
                IMAGEHLP_LINE64 line{};
                line.SizeOfStruct = sizeof(line);
                if (SymGetLineFromAddr64(process, addr, &line_disp, &line))
                    std::fprintf(f, " (%s:%lu)", line.FileName, line.LineNumber);
            }
        }
        std::fprintf(f, "\n");
    }
    if (syms_ok) SymCleanup(process);
}
#else
// Resolves own exe path via /proc/self/exe for addr2line_resolve() below.
// Linux-only (no /proc on macOS) — returns "" there, callers degrade to
// un-annotated backtrace_symbols() output. Same technique as
// ale_monitor/src/ale_monitor.cpp's exe_dir() (locate binary regardless of CWD).
std::string self_exe_path() {
    char buf[4096];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return "";
    buf[n] = '\0';
    return buf;
}

// Best-effort "function at file:line" for one return address via external
// `addr2line` (binutils, present on essentially every Linux install) —
// POSIX equivalent of the Windows dbghelp SymGetLineFromAddr64 call. Appends
// nothing if addr2line is missing, binary has no debug info, or frame can't
// resolve — caller's raw backtrace_symbols() line remains as fallback.
//
// Not async-signal-safe: popen() forks/execs, can deadlock if crash occurred
// while malloc's internal lock was held — same best-effort tradeoff as rest
// of file; alternative is no location info at all.
void addr2line_resolve(FILE* out, const std::string& exe, void* addr) {
    char cmd[4224];
    std::snprintf(cmd, sizeof(cmd), "addr2line -e \"%s\" -f -C -p %p 2>/dev/null",
                  exe.c_str(), addr);
    FILE* p = popen(cmd, "r");
    if (!p) return;
    char line[512];
    if (std::fgets(line, sizeof(line), p)) {
        const size_t len = std::strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';
        // addr2line prints "?? ??:0" with no debug info — skip rather than append useless "-- ?? ??:0".
        if (std::strcmp(line, "?? ??:0") != 0)
            std::fprintf(out, " -- %s", line);
    }
    pclose(p);
}

void write_stack_trace(FILE* f) {
    void* frames[64];
    const int count = backtrace(frames, 64);
    if (count <= 0) {
        std::fprintf(f, "  (backtrace() returned no frames)\n");
        return;
    }
    // Needs -rdynamic at link time (CMakeLists.txt) to name in-exe frames, not
    // just shared libs — else every in-exe frame shows "openALE(+0x1234)" not a name.
    char** symbols = backtrace_symbols(frames, count);
    const std::string exe = self_exe_path();

    for (int i = 0; i < count; ++i) {
        std::fprintf(f, "  #%2d %s", i, symbols ? symbols[i] : "(unknown)");
        if (!exe.empty()) addr2line_resolve(f, exe, frames[i]);
        std::fprintf(f, "\n");
    }
    if (symbols) std::free(symbols);  // backtrace_symbols() malloc()s the array
}
#endif

// Shared record writer for handlers below — tiny, dependency-free on purpose (see file doc comment).
void write_crash_record(const char* cause, const char* detail) {
    FILE* f = fopen_portable(kCrashLogFile, "a");
    if (!f) return;
    std::fprintf(f, "==== CRASH ==== ");
    write_timestamp(f);
    std::fprintf(f, "\ncause: %s\ndetail: %s\nstack:\n", cause, detail ? detail : "(none)");
    write_stack_trace(f);
    std::fprintf(f, "\n");
    std::fflush(f);
    std::fclose(f);
}

void terminate_handler() {
    std::string what = "(no active exception — direct std::terminate() call, "
                        "or a noexcept function threw)";
    const char* cause = "UNCAUGHT_EXCEPTION";
    if (std::exception_ptr eptr = std::current_exception()) {
        try {
            std::rethrow_exception(eptr);
        } catch (const std::exception& e) {
            what = e.what();
        } catch (...) {
            what = "(exception of unknown, non-std::exception type)";
        }
    }
    write_crash_record(cause, what.c_str());
    std::abort();
}

extern "C" void signal_handler(int sig) {
    const char* name =
        sig == SIGABRT ? "SIGABRT" :
        sig == SIGSEGV ? "SIGSEGV" :
        sig == SIGFPE  ? "SIGFPE"  :
        sig == SIGILL  ? "SIGILL"  : "SIGNAL";
    write_crash_record(name, "caught via signal handler");
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}

#ifdef _WIN32
const char* exception_code_name(DWORD code) {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION:      return "EXCEPTION_ACCESS_VIOLATION";
        case EXCEPTION_STACK_OVERFLOW:        return "EXCEPTION_STACK_OVERFLOW";
        case EXCEPTION_ILLEGAL_INSTRUCTION:   return "EXCEPTION_ILLEGAL_INSTRUCTION";
        case EXCEPTION_INT_DIVIDE_BY_ZERO:    return "EXCEPTION_INT_DIVIDE_BY_ZERO";
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:    return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
        case EXCEPTION_PRIV_INSTRUCTION:      return "EXCEPTION_PRIV_INSTRUCTION";
        case EXCEPTION_IN_PAGE_ERROR:         return "EXCEPTION_IN_PAGE_ERROR";
        // 0xE06D7363 ('msc' in low 3 bytes): fixed SEH code MSVC C++ runtime raises
        // for every `throw` — an uncaught C++ exception at this level, distinct from
        // a hardware fault. Common enough (e.g. lqa.bin std::length_error case this
        // handler was written for) to deserve a readable label vs bare hex.
        case 0xE06D7363:                      return "MSVC_CPP_EXCEPTION (uncaught C++ throw)";
        default:                              return "EXCEPTION (see code)";
    }
}

LONG WINAPI seh_filter(EXCEPTION_POINTERS* ep) {
    // Stack-overflow filter runs on exhausted stack — reset guard page first or
    // logging/dbghelp calls below can themselves fault.
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_STACK_OVERFLOW)
        _resetstkoflw();

    char detail[128];
    std::snprintf(detail, sizeof(detail), "code=0x%08lX at address 0x%p",
                  ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress);
    write_crash_record(exception_code_name(ep->ExceptionRecord->ExceptionCode), detail);
    return EXCEPTION_EXECUTE_HANDLER;  // terminate quietly — no WER "stopped working" dialog
}
#endif

} // anonymous namespace

void install_crash_handler() {
    std::set_terminate(terminate_handler);
    std::signal(SIGABRT, signal_handler);
    std::signal(SIGSEGV, signal_handler);
    std::signal(SIGFPE,  signal_handler);
    std::signal(SIGILL,  signal_handler);
#ifdef _WIN32
    SetUnhandledExceptionFilter(seh_filter);
#endif
}

} // namespace pal
