add_library(wemod_patcher STATIC patcher.cpp patcher.hpp)
add_library(wemod::patcher ALIAS wemod_patcher)

target_compile_features(wemod_patcher PUBLIC cxx_std_23)

target_include_directories(
    wemod_patcher PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}>
                         $<INSTALL_INTERFACE:include>)

# Implementation-only deps: the public header is pure std C++23.
# nlohmann_json reads/writes the ASAR header, mbedcrypto provides SHA-256.
# Ref: https://github.com/nlohmann/json
# Ref: https://github.com/Mbed-TLS/mbedtls
target_link_libraries(wemod_patcher PRIVATE nlohmann_json::nlohmann_json
                                             MbedTLS::mbedcrypto)

set_target_properties(
    wemod_patcher PROPERTIES MSVC_RUNTIME_LIBRARY
                             "MultiThreaded$<$<CONFIG:Debug>:Debug>")
