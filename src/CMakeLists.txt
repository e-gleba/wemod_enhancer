add_library(version SHARED library.c fuses.c library.def)
set_target_properties(version PROPERTIES PREFIX "" OUTPUT_NAME "version")
target_compile_features(version PRIVATE c_std_11)

if(MSVC)
    set_property(TARGET version PROPERTY
                 MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    target_link_options(version PRIVATE -static)
endif()

include(GNUInstallDirs)
install(TARGETS version RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
                        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}")
