// log_spdlog.cpp - the ONLY file including <spdlog/**>. Owns the
// registry logger: color console + file next to the exe. Every other
// subsystem logs through log.hpp and never knows spdlog exists.

#include "log.hpp"

#include <spdlog/common.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <memory>
#include <mutex>
#include <vector>

namespace wemod::gui::log
{

namespace
{

std::mutex mutex;
std::vector<Sink> sinks;
fs::path file_path;
bool ready{false};

[[nodiscard]] spdlog::level::level_enum to_spd(Level level) noexcept
{
    switch (level) {
    case Level::trace:
        return spdlog::level::trace;
    case Level::debug:
        return spdlog::level::debug;
    case Level::warn:
        return spdlog::level::warn;
    case Level::error:
        return spdlog::level::err;
    case Level::critical:
        return spdlog::level::critical;
    case Level::info:
    default:
        return spdlog::level::info;
    }
}

void ensure_console() noexcept
{
    try {
        if (spdlog::get("wemod") != nullptr) {
            return;
        }
        auto console{std::make_shared<spdlog::sinks::stdout_color_sink_mt>()};
        auto logger{std::make_shared<spdlog::logger>("wemod", console)};
        logger->set_level(spdlog::level::debug);
        logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
        spdlog::register_logger(logger);
        spdlog::set_default_logger(logger);
        ready = true;
    } catch (...) {
    }
}

} // namespace

void add_sink(Sink sink) noexcept
{
    try {
        const std::lock_guard<std::mutex> lock{mutex};
        sinks.push_back(std::move(sink));
    } catch (...) {
    }
}

void set_file(fs::path path) noexcept
{
    try {
        const std::lock_guard<std::mutex> lock{mutex};
        file_path = std::move(path);
    } catch (...) {
    }
}

void init(fs::path file) noexcept
{
    try {
        {
            const std::lock_guard<std::mutex> lock{mutex};
            file_path = std::move(file);
        }
        ensure_console();
        auto logger{spdlog::get("wemod")};
        if (logger == nullptr) {
            return;
        }
        try {
            // Truncate per launch: one session per file, no rotation
            // config needed. Future: rotating_file_sink_mt when the
            // log outlives a single run.
            auto file_sink{
                std::make_shared<spdlog::sinks::basic_file_sink_mt>(
                    file_path.string(), true)};
            file_sink->set_level(spdlog::level::trace);
            logger->sinks().push_back(file_sink);
        } catch (...) {
            // Console-only fallback: file failures never break startup.
        }
        logger->flush_on(spdlog::level::warn);
        spdlog::flush_every(std::chrono::seconds(2));
    } catch (...) {
    }
}

void shutdown() noexcept
{
    try {
        spdlog::shutdown();
    } catch (...) {
    }
    try {
        const std::lock_guard<std::mutex> lock{mutex};
        sinks.clear();
        ready = false;
    } catch (...) {
    }
}

void write(Level level, std::string_view message) noexcept
{
    std::vector<Sink> local;
    try {
        const std::lock_guard<std::mutex> lock{mutex};
        local = sinks;
    } catch (...) {
    }
    for (auto& sink : local) {
        try {
            sink(level, message);
        } catch (...) {
        }
    }
    try {
        ensure_console();
        if (auto logger{spdlog::get("wemod")}) {
            logger->log(to_spd(level), std::string(message));
        }
    } catch (...) {
    }
    (void)ready;
}

} // namespace wemod::gui::log
