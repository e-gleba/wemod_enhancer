# Patcher static library: C++23 port of scripts/wemod_enhancer.py.
add_library(wemod_patcher STATIC patcher.cpp)
add_library(wemod::patcher ALIAS wemod_patcher)
target_compile_features(wemod_patcher PUBLIC cxx_std_23)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(mbedtls CONFIG REQUIRED)
find_package(ctre CONFIG REQUIRED)
target_link_libraries(
    wemod_patcher
    PRIVATE
        nlohmann_json::nlohmann_json
        MbedTLS::mbedcrypto
        ctre::ctre)
target_include_directories(
    wemod_patcher
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>)
