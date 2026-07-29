#pragma once

#include <set>
#include <string>

namespace egleba::doctest {
    [[nodiscard]] std::set<std::string> get_all_tests();
}
