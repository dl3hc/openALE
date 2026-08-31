// paths.cpp — pal::user_config_dir() implementation.

#include "PAL/paths.h"

#include <cstdlib>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <sys/stat.h>
#endif

namespace pal {

namespace {

#ifdef _WIN32
bool ensure_dir(const std::string& path) {
    if (CreateDirectoryA(path.c_str(), nullptr)) return true;
    return GetLastError() == ERROR_ALREADY_EXISTS;
}
#else
bool ensure_dir(const std::string& path) {
    if (::mkdir(path.c_str(), 0700) == 0) return true;
    return errno == EEXIST;
}
#endif

} // namespace

std::string user_config_dir() {
#ifdef _WIN32
    char buf[MAX_PATH];
    const DWORD n = GetEnvironmentVariableA("APPDATA", buf, static_cast<DWORD>(sizeof(buf)));
    if (n == 0 || n >= sizeof(buf)) return {};
    const std::string dir = std::string(buf, n) + "\\openALE";
    if (!ensure_dir(dir)) return {};
    return dir;
#else
    std::string base;
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) {
        base = xdg;
    } else if (const char* home = std::getenv("HOME"); home && *home) {
        base = std::string(home) + "/.config";
    } else {
        return {};
    }
    // base (~/.config) is assumed to already exist on any normal system;
    // only the openALE subdirectory needs creating.
    const std::string dir = base + "/openALE";
    if (!ensure_dir(dir)) return {};
    return dir;
#endif
}

} // namespace pal
