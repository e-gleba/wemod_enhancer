cpmaddpackage(
    NAME
    lua
    GIT_REPOSITORY
    https://github.com/lua/lua.git
    VERSION
    5.5.1
    DOWNLOAD_ONLY
    YES)

if(lua_ADDED)
    file(
        GLOB
        lua_sources
        CONFIGURE_DEPENDS
        "${lua_SOURCE_DIR}/*.c")
    list(
        REMOVE_ITEM
        lua_sources
        "${lua_SOURCE_DIR}/lua.c" # Interpreter
        "${lua_SOURCE_DIR}/luac.c" # Compiler
        "${lua_SOURCE_DIR}/onelua.c" # Single-file build
    )

    add_library(lua STATIC ${lua_sources})
    add_library(lua::lua ALIAS lua)

    target_include_directories(lua SYSTEM
                               PUBLIC "$<BUILD_INTERFACE:${lua_SOURCE_DIR}>")

    target_compile_features(lua PUBLIC c_std_23)

    # Professional Lua configuration
    target_compile_definitions(
        lua
        PUBLIC LUA_COMPAT_5_3=0 # Disable Lua 5.3 compatibility bloat
               $<$<CONFIG:Debug>:LUA_USE_APICHECK> # API checking in debug only
    )

    # Performance: computed goto (GCC/Clang only)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_definitions(lua PRIVATE LUA_USE_JUMPTABLE)
    endif()
endif()
