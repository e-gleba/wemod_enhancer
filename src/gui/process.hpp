// process.hpp - child-process contract. Knows nothing about logging,
// platform, or the model. process_popen.cpp is the only implementation.

#pragma once

#include <cstdint>
#include <string>

namespace wemod::gui
{

namespace process
{

struct Result final
{
    std::int32_t exit_code{-1};
    std::string output;
};

// Run through the system shell, capture merged stdout+stderr.
// Never throws: failures become Result{-1, "error: ..."}.
[[nodiscard]] Result run_capture(const std::string& command) noexcept;

} // namespace process

} // namespace wemod::gui
