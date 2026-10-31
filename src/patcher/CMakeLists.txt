# Patcher object library: C++23 port of scripts/wemod_enhancer.py.
# OBJECT (not STATIC): the GUI and any future consumer link the objects
# directly, so there is exactly one copy of the patcher code in the tree.
add_library(wemod_patcher OBJECT patcher.cpp patcher.hpp)
add_library(wemod::patcher ALIAS wemod_patcher)
target_compile_features(wemod_patcher PUBLIC cxx_std_23)
# PUBLIC: an OBJECT library produces no binary to absorb these, so usage
# requirements must propagate to whatever links the objects.
# tfpsacrypto (not mbedcrypto): Mbed TLS 4.x moved all crypto, including
# SHA-256, into TF-PSA-Crypto; patcher.cpp uses the PSA hash API.
# Ref: https://github.com/Mbed-TLS/mbedtls/blob/development/docs/4.0-migration-guide.md
target_link_libraries(
    wemod_patcher
    PUBLIC
        nlohmann_json::nlohmann_json
        MbedTLS::tfpsacrypto
        ctre::ctre)
target_include_directories(
    wemod_patcher
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>)
