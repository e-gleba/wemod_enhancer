cmake_minimum_required(VERSION 3.30)

add_executable(unity_test)

target_sources(unity_test PRIVATE app.cpp greet.cpp)

target_link_libraries(
    unity_test PRIVATE SDL3::SDL3-shared Microsoft.GSL::GSL warnings
)

target_compile_features(unity_test PRIVATE cxx_std_23)

# ── Unity build: the whole point of this project ──────────────
set_target_properties(
    unity_test PROPERTIES UNITY_BUILD ON UNITY_BUILD_BATCH_SIZE
                                         0 # 0 = merge ALL sources into one TU
)
