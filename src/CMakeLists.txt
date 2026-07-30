# Windows version.dll proxy. Forwarded exports live in library.def; C++ only
# contains process-attach behavior.

set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

set(forwarder_definition "${CMAKE_CURRENT_SOURCE_DIR}/library.def")
set(forwarder_import_definition
    "${CMAKE_CURRENT_BINARY_DIR}/version_orig.def")

file(READ "${forwarder_definition}" forwarder_import_content)
string(REGEX REPLACE "=version_orig\\.[A-Za-z0-9_]+" ""
                     forwarder_import_content "${forwarder_import_content}")
string(REGEX REPLACE "LIBRARY[ \t]+\"?version\"?" "LIBRARY version_orig"
                     forwarder_import_content "${forwarder_import_content}")
file(CONFIGURE OUTPUT "${forwarder_import_definition}"
     CONTENT "${forwarder_import_content}" @ONLY)

if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    set(forwarder_import_library
        "${CMAKE_CURRENT_BINARY_DIR}/version_orig.lib")
    add_custom_command(
        OUTPUT "${forwarder_import_library}"
        COMMAND
            "${CMAKE_AR}" /DEF:${forwarder_import_definition} /MACHINE:X64
            /OUT:${forwarder_import_library}
        DEPENDS "${forwarder_definition}"
        COMMENT "Generating version_orig import library"
        VERBATIM)
elseif(WIN32)
    set(forwarder_import_library
        "${CMAKE_CURRENT_BINARY_DIR}/version_orig.lib")
    add_custom_command(
        OUTPUT "${forwarder_import_library}"
        COMMAND
            "${CMAKE_LINKER}" /lib /def:${forwarder_import_definition}
            /machine:x64 /out:${forwarder_import_library}
        DEPENDS "${forwarder_definition}"
        COMMENT "Generating version_orig import library"
        VERBATIM)
else()
    set(forwarder_import_library
        "${CMAKE_CURRENT_BINARY_DIR}/libversion_orig.a")
    add_custom_command(
        OUTPUT "${forwarder_import_library}"
        COMMAND
            "${CMAKE_DLLTOOL}" -d "${forwarder_import_definition}"
            -m i386:x86-64 -l "${forwarder_import_library}"
        DEPENDS "${forwarder_definition}"
        COMMENT "Generating version_orig import library"
        VERBATIM)
endif()

add_custom_target(forwarder_import DEPENDS "${forwarder_import_library}")

add_library(
    version SHARED
    dll_main.cpp
    fuses.cpp
    fuses.hpp
    library.def)
add_dependencies(version forwarder_import)

target_compile_features(version PRIVATE cxx_std_23)
target_compile_definitions(version PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(
    version
    PRIVATE "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>")
target_link_libraries(version PRIVATE GSL "${forwarder_import_library}")
target_link_options(
    version
    PRIVATE
        $<$<AND:$<CXX_COMPILER_ID:Clang>,$<NOT:$<CXX_COMPILER_FRONTEND_VARIANT:MSVC>>>:-static>)

set_target_properties(
    version
    PROPERTIES PREFIX ""
               OUTPUT_NAME "version"
               CXX_EXTENSIONS OFF
               CXX_SCAN_FOR_MODULES OFF
               MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(GNUInstallDirs)
install(
    TARGETS version
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
