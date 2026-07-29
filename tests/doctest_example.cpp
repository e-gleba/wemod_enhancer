#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include <doctest/doctest.h>

DOCTEST_TEST_CASE_TEMPLATE("arithmetic identity", t, int, float, double)
{
    constexpr t zero = t{};
    DOCTEST_CHECK_EQ(t{ 1 } + zero, t{ 1 });
    DOCTEST_CHECK_EQ(t{ 42 } * t{ 1 }, t{ 42 });
}

DOCTEST_TEST_SUITE("math playground")
{
    static constexpr std::int32_t factorial(std::int32_t n) noexcept
    {
        return (n <= 1) ? 1 : n * factorial(n - 1);
    }

    DOCTEST_TEST_CASE("factorial" * doctest::description("recursive, noexcept"))
    {
        DOCTEST_SUBCASE("base cases")
        {
            DOCTEST_CHECK_EQ(factorial(0), 1);
            DOCTEST_CHECK_EQ(factorial(1), 1);
        }
        DOCTEST_SUBCASE("positive values")
        {
            DOCTEST_CHECK_EQ(factorial(5), 120);
            DOCTEST_CHECK_EQ(factorial(10), 3'628'800);
        }
        DOCTEST_SUBCASE("monotonicity")
        {
            for (int i = 2; i <= 10; ++i) {
                DOCTEST_CAPTURE(i);
                DOCTEST_CHECK_GT(factorial(i), factorial(i - 1));
            }
        }
    }
}

DOCTEST_TEST_CASE("string length")
{
    const std::vector<std::pair<std::string, std::size_t>> data{
        { "", 0 },
        { "hello", 5 },
        { "привет", 12 },
        { std::string(1'000, 'x'), 1'000 }
    };

    for (const auto& [input, expected] : data) {
        const auto label =
            input.empty() ? std::string{ "<empty>" } : input.substr(0, 20);
        DOCTEST_SUBCASE(label.c_str())
        {
            DOCTEST_CAPTURE(input);
            DOCTEST_CHECK_EQ(input.size(), expected);
        }
    }
}

namespace {
    void throw_out_of_range() {
        throw std::out_of_range("bounds");
    }

    void throw_logic_check() {
        throw std::logic_error("vector::_M_range_check");
    }

    void noop() {}
}

DOCTEST_TEST_CASE("exception contracts" * doctest::timeout(0.1))
{
    DOCTEST_CHECK_THROWS_AS(throw_out_of_range(), std::out_of_range);
    DOCTEST_CHECK_THROWS_WITH(throw_logic_check(),
                              doctest::Contains("vector::_M_range_check"));
    DOCTEST_CHECK_NOTHROW(noop());
}

namespace {
    template<typename t>
    t clamp(t v, t lo, t hi) noexcept {
        if (v < lo) {
            return lo;
        }
        if (hi < v) {
            return hi;
        }
        return v;
    }
}

DOCTEST_TEST_CASE_TEMPLATE("clamp", t, int, float, double)
{
    DOCTEST_SUBCASE("within range")
    {
        DOCTEST_CHECK_EQ(clamp(t{ 5 }, t{ 0 }, t{ 10 }), t{ 5 });
    }
    DOCTEST_SUBCASE("below minimum")
    {
        DOCTEST_CHECK_EQ(clamp(t{ -3 }, t{ 0 }, t{ 10 }), t{ 0 });
    }
    DOCTEST_SUBCASE("above maximum")
    {
        DOCTEST_CHECK_EQ(clamp(t{ 99 }, t{ 0 }, t{ 10 }), t{ 10 });
    }
    DOCTEST_SUBCASE("edge: lo==hi")
    {
        DOCTEST_CHECK_EQ(clamp(t{ 5 }, t{ 0 }, t{ 0 }), t{ 0 });
    }
}

DOCTEST_TEST_CASE("unimplemented feature" * doctest::skip(true))
{
    DOCTEST_FAIL("not yet implemented");
}

DOCTEST_TEST_CASE("flaky platform test" * doctest::may_fail(true))
{
    DOCTEST_CHECK(std::abs(std::sin(0.0)) < 1e-15);
}

DOCTEST_TEST_CASE("diagnostic output")
{
    DOCTEST_INFO("executing diagnostic test");
    int value = 42;
    DOCTEST_CAPTURE(value);
    DOCTEST_MESSAGE("value is expected to be 42", doctest::assertType::is_warn);
    DOCTEST_CHECK_EQ(value, 42);
}
