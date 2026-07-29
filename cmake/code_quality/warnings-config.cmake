add_library(warnings INTERFACE)

target_compile_options(
    warnings
    INTERFACE
        "$<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wno-unused-function>"
        "$<$<COMPILE_LANG_AND_ID:C,GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wno-unused-function>"
        "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/W4;/wd4100;/wd4505>"
        "$<$<COMPILE_LANG_AND_ID:C,MSVC>:/W4>")

# 3.24+: let cmake handle -Werror / /WX portably
set_target_properties(warnings PROPERTIES INTERFACE_COMPILE_WARNING_AS_ERROR ON)
