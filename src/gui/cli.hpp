#pragma once

#include <cstdint>
#include <span>
#include <string>

namespace wemod::gui::cli
{

enum class mode : std::uint8_t { gui, exit_ok, exit_err };

struct decision final
{
    mode kind = mode::gui;
    std::string output;
};

[[nodiscard]] decision decide(std::span<char*> args) noexcept;

} // namespace wemod::gui::cli
