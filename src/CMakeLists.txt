add_library(version SHARED fuses.c library.c library.def)

set_target_properties(
    version
    PROPERTIES PREFIX ""
               OUTPUT_NAME "version"
               MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

target_compile_features(version PRIVATE c_std_11)

target_link_options(version PRIVATE
                    "$<$<LINK_LANG_AND_ID:C,GNU,Clang>:-static>")

include(GNUInstallDirs)
install(
    TARGETS version
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}" # MSVC import .lib
)
