#include <functional>
#include <ranges>
#include <span>

// Enable doctest introspection
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

int main(int argc, char* argv[], [[maybe_unused]] char* envp[])
{
    doctest::Context ctx{ argc, argv };
    ctx.setOption("duration", true);

    return ctx.run();
}

namespace egleba::doctest {
    [[nodiscard]] auto get_all_tests() -> std::set<std::string> {
        const std::set<::doctest::detail::TestCase> &registered =
                ::doctest::detail::getRegisteredTests();

        auto names = registered | std::views::transform([](const auto &tc) {
            return std::string{tc.m_name};
        });

#if __cplusplus >= 202302L
        return std::ranges::to<std::set>(names);
#else
        return { std::ranges::begin(names), std::ranges::end(names) };
#endif
    }
}
