# Windows version.dll proxy with forwarder exports
#
# The proxy forwards all version API exports to version_orig.dll (a renamed
# copy of the real system version.dll). MSVC link.exe requires an import
# library for the forwarder target to resolve symbols at link time.

set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# Generate import library for forwarder target
# MSVC link.exe needs version_orig.lib to resolve forwarder symbols.
# lld-link and lld embed forwarder strings without it, but linking is harmless.

function(generate_forwarder_import_lib def_file output_lib)
    file(READ "${def_file}" def_content)
    string(REGEX MATCHALL "([A-Za-z0-9_]+)=version_orig\\.[A-Za-z0-9_]+" 
           exports "${def_content}")
    
    set(import_def_content "LIBRARY version_orig\nEXPORTS\n")
    foreach(export ${exports})
        string(REGEX REPLACE "=version_orig\\.[A-Za-z0-9_]+" "" 
               export_name "${export}")
        string(APPEND import_def_content "    ${export_name}\n")
    endforeach()
    
    set(import_def "${CMAKE_CURRENT_BINARY_DIR}/version_orig_import.def")
    file(WRITE "${import_def}" "${import_def_content}")
    
    if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC" 
       OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        add_custom_command(
            OUTPUT "${output_lib}"
            COMMAND "${CMAKE_AR}" /DEF:${import_def} /MACHINE:X64 /OUT:${output_lib}
            DEPENDS "${import_def}"
            COMMENT "Generating version_orig import library (lib.exe)"
            VERBATIM)
    else()
        find_program(dlltool_executable
            NAMES llvm-dlltool dlltool
            HINTS "${CMAKE_CXX_COMPILER}"
            PATH_SUFFIXES ../bin bin
            REQUIRED)
        
        add_custom_command(
            OUTPUT "${output_lib}"
            COMMAND "${dlltool_executable}" -d "${import_def}" 
                    -m "i386:x86-64" -l "${output_lib}"
            DEPENDS "${import_def}"
            COMMENT "Generating version_orig import library (dlltool)"
            VERBATIM)
    endif()
endfunction()

set(import_lib "${CMAKE_CURRENT_BINARY_DIR}/version_orig.lib")
generate_forwarder_import_lib(
    "${CMAKE_CURRENT_SOURCE_DIR}/library.def" 
    "${import_lib}")

# Proxy target
add_library(version SHARED
    dll_main.cpp
    fuses.cpp
    fuses.hpp
    library.def)

target_compile_features(version PRIVATE cxx_std_23)
target_compile_definitions(version PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(version PRIVATE 
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>")

target_link_libraries(version PRIVATE GSL "${import_lib}")

target_link_options(version PRIVATE
    $<$<AND:$<CXX_COMPILER_ID:Clang>,$<NOT:$<CXX_COMPILER_FRONTEND_VARIANT:MSVC>>>:-static>)

set_target_properties(version PROPERTIES
    PREFIX ""
    OUTPUT_NAME "version"
    CXX_EXTENSIONS OFF
    CXX_SCAN_FOR_MODULES OFF
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(GNUInstallDirs)
install(TARGETS version
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
