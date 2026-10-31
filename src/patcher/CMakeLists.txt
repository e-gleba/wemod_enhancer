add_library(wemod_patcher STATIC asar.cpp bundles.cpp patcher.cpp asar.hpp bundles.hpp patcher.hpp)
add_library(wemod::patcher ALIAS wemod_patcher)

target_compile_features(wemod_patcher PUBLIC cxx_std_23)

target_include_directories(wemod_patcher PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
                                               $<INSTALL_INTERFACE:include>)

target_link_libraries(wemod_patcher PRIVATE nlohmann_json::nlohmann_json picosha2::picosha2 warnings)

set_target_properties(wemod_patcher PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
