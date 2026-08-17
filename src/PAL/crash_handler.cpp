// crash_handler.cpp — writes crash.log on an unhandled exception/fault.
//
// Deliberately uses raw fopen/fprintf instead of pal::get_logger(): this code
// runs after the process is already in an undefined state (corrupted heap,
// exhausted stack, mid-unwind), so it must depend on as little of the rest of
// the program as possible — the same rationale documented for logger.cpp in
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
// Best-effort: walks the current call stack and resolves symbols via dbghelp
// when a matching PDB is available (Debug builds). SymInitialize() is cheap
// enough to call fresh on every crash record — there is at most one per
// process lifetime in practice.
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
// Resolves the running executable's own path via /proc/self/exe, so
// addr2line_resolve() below can be pointed at it. Linux-only (no /proc on
// macOS) — self_exe_path() returns "" there and callers degrade gracefully
// to the un-annotated backtrace_symbols() output. Mirrors the technique
// ale_monitor/src/ale_monitor.cpp's exe_dir() already uses for the same
// reason (locating the binary regardless of CWD).
std::string self_exe_path() {
    char buf[4096];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return "";
    buf[n] = '\0';
    return buf;
}

// Best-effort "function at file:line" resolution for one return address via
// the external `addr2line` tool (binutils — present on essentially every
// Linux install), the closest POSIX equivalent to the Windows branch's
// dbghelp SymGetLineFromAddr64 call. Appends nothing if addr2line is
// missing, the binary has no debug info, or the frame can't be resolved —
// the raw backtrace_symbols() line printed by the caller is always there as
// a fallback either way.
//
// Not async-signal-safe in the strict POSIX sense (popen() forks/execs,
// which can deadlock if the crash happened while malloc's internal lock was
// already held) — same "best effort, not a hardened crash reporter"
// tradeoff already accepted for the rest of this file; the alternative is no
// location information at all.
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
        // addr2line prints "?? ??:0" when it has no debug info for a frame —
        // skip that noise rather than appending a useless "-- ?? ??:0".
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
    // Needs -rdynamic at link time (see CMakeLists.txt) to name frames inside
    // the executable itself, not just shared libraries — without it every
    // in-exe frame shows only "openALE(+0x1234)" instead of a function name.
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

// Shared record writer for every handler below — kept tiny and dependency-
// free on purpose (see file doc comment).
void write_crash_record(const char* cause, const char* detail) {
    FILE* f = std::fopen(kCrashLogFile, "a");
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
        // 0xE06D7363 ('msc' encoded into the low 3 bytes) is the fixed SEH
        // exception code the MSVC C++ runtime raises for every `throw` — this
        // is what an uncaught C++ exception looks like at this level, distinct
        // from an actual hardware fault. Common enough in practice (e.g. the
        // lqa.bin std::length_error scenario this handler was written for)
        // that it deserves a readable label instead of a bare hex code.
        case 0xE06D7363:                      return "MSVC_CPP_EXCEPTION (uncaught C++ throw)";
        default:                              return "EXCEPTION (see code)";
    }
}

LONG WINAPI seh_filter(EXCEPTION_POINTERS* ep) {
    // A stack-overflow filter runs on an exhausted stack — reset the guard
    // page first or logging/dbghelp calls below can themselves fault.
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
