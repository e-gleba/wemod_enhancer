if(WEMOD_ENHANCER_BUILD_GUI)
    add_subdirectory(gui)
    return()
endif()

add_library(version SHARED fuses.c library.c library.def)

set_target_properties(
    version
    PROPERTIES PREFIX ""
               OUTPUT_NAME "version"
               MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

target_compile_features(version PRIVATE c_std_11)

target_link_options(
    version
    PRIVATE
    "$<$<AND:$<C_COMPILER_ID:Clang>,$<NOT:$<C_COMPILER_FRONTEND_VARIANT:MSVC>>>:-static>"
)

install(TARGETS version)
