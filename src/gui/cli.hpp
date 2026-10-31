#pragma once

#include <string>

namespace wemod::gui::cli
{

enum class mode : unsigned char { gui, exit_ok, exit_err };

struct decision final
{
    mode mode{mode::gui};
    std::string output;
};

[[nodiscard]] decision decide(int argc, char* argv[]) noexcept;

} // namespace wemod::gui::cli
