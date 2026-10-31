# Patcher object library: C++23 port of scripts/wemod_enhancer.py.
# OBJECT (not STATIC): the GUI and any future consumer link the objects
# directly, so there is exactly one copy of the patcher code in the tree.
add_library(wemod_patcher OBJECT patcher.cpp patcher.hpp)
add_library(wemod::patcher ALIAS wemod_patcher)
target_compile_features(wemod_patcher PUBLIC cxx_std_23)
# PUBLIC: an OBJECT library produces no binary to absorb these, so usage
# requirements must propagate to whatever links the objects.
# wemod_tfpsacrypto: alias from cmake/cpm/mbedtls-config.cmake for the
# Mbed TLS 4.x crypto target (plain `tfpsacrypto` as a subproject).
target_link_libraries(
    wemod_patcher
    PUBLIC
        nlohmann_json::nlohmann_json
        wemod_tfpsacrypto
        ctre::ctre)
target_include_directories(
    wemod_patcher
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>)
