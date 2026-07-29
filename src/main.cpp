#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <version>

[[nodiscard]] consteval const char* detect_os() noexcept
{
#if defined(_WIN32) || defined(_WIN64)
    return "Windows";
#elif defined(__linux__)
    return "Linux";
#elif defined(__APPLE__) && defined(__MACH__)
    return "macOS";
#elif defined(__FreeBSD__)
    return "FreeBSD";
#elif defined(__unix__) || defined(__unix)
    return "Unix";
#else
    return "Unknown";
#endif
}

struct compiler_info final
{
    const char* name;
    int         version;
};

[[nodiscard]] consteval compiler_info detect_compiler() noexcept
{
#if defined(__clang__)
    return { .name = "Clang", .version = __clang_major__ };
#elif defined(__GNUC__)
    return { .name = "GCC", .version = __GNUC__ };
#elif defined(_MSC_VER)
    return { .name = "MSVC", .version = _MSC_VER / 100 };
#else
    return { .name = "Unknown", .version = 0 };
#endif
}

[[nodiscard]] consteval long detect_std() noexcept
{
#if defined(_MSVC_LANG)
    return _MSVC_LANG;
#else
    return __cplusplus;
#endif
}

int main()
{
    constexpr auto os       = detect_os();
    constexpr auto compiler = detect_compiler();
    constexpr auto cpp_std  = detect_std();

    std::cout << "System Info\n"
              << "  OS:           " << os << '\n'
              << "  Compiler:     " << compiler.name << ' ' << compiler.version
              << '\n'
              << "  C++ Standard: " << cpp_std << '\n'
              << "\nHello, World!\n";

    // flush both stdio and iostream — they share fd but not buffers
    std::cout.flush();
    std::fflush(stdout);

    if (!std::cout.good() || std::ferror(stdout) != 0) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
