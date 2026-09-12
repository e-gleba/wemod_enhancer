// log.hpp - narrow logging interface, the only logging contract.
// No subsystem includes <spdlog/**> except log_spdlog.cpp. Everyone
// else logs through wemod::gui::log:: below; swapping the sink only
// touches one translation unit.
//
// Levels mirror spdlog; messages also fan out to every registered
// sink (console mirror, file, message box) via add_sink().

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

namespace wemod::gui
{

namespace fs = std::filesystem;

namespace log
{

enum class Level : std::uint8_t { trace, debug, info, warn, error, critical };

// Extra destination for log lines (console mirror, file tail ...).
// Called on the logging thread; keep it short and non-blocking.
using Sink = std::function<void(Level, std::string_view)>;

// Register an extra sink. Thread-safe, never throws.
void add_sink(Sink sink) noexcept;
// Point the file sink at path (created on init()). Never throws.
void set_file(fs::path path) noexcept;
// Console + file (next to the exe). Safe to call twice; second call
// only re-points the file. Never throws - failure keeps console only.
void init(fs::path file) noexcept;
// Flush file sink. Never throws.
void shutdown() noexcept;

void write(Level level, std::string_view message) noexcept;

inline void trace(std::string_view m) noexcept { write(Level::trace, m); }
inline void debug(std::string_view m) noexcept { write(Level::debug, m); }
inline void info(std::string_view m) noexcept { write(Level::info, m); }
inline void warn(std::string_view m) noexcept { write(Level::warn, m); }
inline void error(std::string_view m) noexcept { write(Level::error, m); }
inline void critical(std::string_view m) noexcept
{
    write(Level::critical, m);
}

} // namespace log

} // namespace wemod::gui
