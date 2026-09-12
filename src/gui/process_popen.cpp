// process_popen.cpp - the ONLY file implementing process.hpp.
// Talks popen/_popen + log:: only. Knows nothing about platform,
// Model, view, or the presenter.

#include "process.hpp"

#include "log.hpp"

#include <array>
#include <cerrno>
#include <cstdio>
#include <format>
#include <system_error>

#ifndef _WIN32
#include <sys/wait.h> // WIFEXITED / WEXITSTATUS for pclose()
#endif

namespace wemod::gui::process
{

Result run_capture(const std::string& command) noexcept
{
    Result result;
    try {
        // cmd.exe (_popen) supports 2>&1 too: without it the patcher's
        // Python tracebacks bypass result.output and the bug report
        // stays empty on failure.
        const std::string full{command + " 2>&1"};

#ifdef _WIN32
        FILE* pipe{_popen(full.c_str(), "r")};
#else
        FILE* pipe{popen(full.c_str(), "r")};
#endif
        if (pipe == nullptr) {
            result.output =
                std::format("error: failed to start the command ({})",
                            std::system_category().message(errno));
            log::error(result.output);
            return result;
        }

        std::array<char, 4096> buffer{};
        while (fgets(buffer.data(),
                     static_cast<int>(buffer.size()), pipe) != nullptr) {
            result.output += buffer.data();
        }

#ifdef _WIN32
        result.exit_code = _pclose(pipe);
#else
        const int status{pclose(pipe)};
        result.exit_code =
            status == -1 || !WIFEXITED(status) ? -1 : WEXITSTATUS(status);
#endif
        if (result.exit_code != 0) {
            log::warn(std::format("command exited {}: {}",
                                  result.exit_code, command));
        }
        return result;
    } catch (const std::exception& ex) {
        log::error(std::string("run_capture: ") + ex.what());
        return Result{-1, std::string("error: ") + ex.what() + "\n"};
    } catch (...) {
        log::error("run_capture: unknown error");
        return {-1, "error: background job failed\n"};
    }
}

} // namespace wemod::gui::process
