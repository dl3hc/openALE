/**
 * @file PAL/paths.h
 * @brief Stable per-user config/data directory resolution.
 *
 * openALE's persistent files (station.state, lqa.bin, the Location Relay
 * identity key) default to a bare relative filename resolved against the
 * process's current working directory — fine for a portable install running
 * from a fixed launch script, but it means launching from a different
 * directory (or losing that directory) looks like a first run: config is
 * "gone" and, for Location Relay specifically, a brand-new signing identity
 * gets generated and has to be re-approved by the relay operator.
 *
 * user_config_dir() gives callers (apps/ale_bridge.cpp's main()) a stable
 * fallback location to default to instead, while portable installs (a
 * station.state already sitting in the launch directory) keep working
 * exactly as before — see the call site for the actual precedence logic.
 */
#pragma once

#include <string>

namespace pal {

/// Resolves (and creates if missing) the stable per-user config directory for
/// openALE: `%APPDATA%\openALE` on Windows, `$XDG_CONFIG_HOME/openALE` or
/// `~/.config/openALE` on Linux. Returns an empty string only on an
/// unrecoverable environment/filesystem error — callers should fall back to
/// the current working directory (today's behavior) in that case.
std::string user_config_dir();

} // namespace pal
