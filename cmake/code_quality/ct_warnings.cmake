# --- warnings ----------------------------------------------------------------
# INTERFACE target carrying the project's warning flags.
# Link it PRIVATE into every first-party target - never PUBLIC: consumers
# of an installed package must not inherit our warnings.

# Double inclusion would error on the duplicate add_library - guard it.
include_guard(GLOBAL)

add_library(warnings INTERFACE)

target_compile_options(
    warnings
    INTERFACE
        "$<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wno-unused-function>"
        "$<$<COMPILE_LANG_AND_ID:C,GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wno-unused-function>"
        "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/W4;/wd4100;/wd4505>"
        "$<$<COMPILE_LANG_AND_ID:C,MSVC>:/W4>")

# Let CMake handle -Werror / /WX portably.
set_target_properties(warnings PROPERTIES INTERFACE_COMPILE_WARNING_AS_ERROR ON)
