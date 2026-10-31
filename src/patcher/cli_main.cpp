// CLI entry point: `wemod_enhancer patch --install-dir <dir>` and the
// matching restore/help/version forms. Built as its own binary next to the
// GUI until the single-binary entry point lands.

#include "cli.hpp"

#include <string>

int main(int argc, char* argv[])
{
    std::string program{argv[0] != nullptr ? argv[0] : "wemod_enhancer"};
    if (const std::size_t slash{program.find_last_of("/\\")}; slash != std::string::npos) {
        program.erase(0UZ, slash + 1UZ);
    }
    return wemod::cli::run(program, wemod::cli::parse(argc, argv));
}
