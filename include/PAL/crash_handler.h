/**
 * @file crash_handler.h
 * @brief Process-wide crash handler — writes a diagnosable crash.log instead
 *        of letting an unexpected exit vanish without a trace.
 */

#pragma once

namespace pal {

/**
 * Installs handlers for unhandled SEH exceptions (access violation, stack
 * overflow, ...), uncaught C++ exceptions (std::terminate), and SIGABRT/
 * SIGSEGV, all of which write a timestamped cause + best-effort stack trace
 * to crash.log (append mode, in the process's working directory) before the
 * process exits. Call once, as early as possible in main() — right after
 * pal::set_logger(), before anything else can throw or fault.
 */
void install_crash_handler();

} // namespace pal
