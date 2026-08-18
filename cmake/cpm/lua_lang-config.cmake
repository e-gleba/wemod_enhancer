cpmaddpackage(
    NAME
    lua
    GITHUB_REPOSITORY
    lua/lua
    VERSION
    5.5.0
    EXCLUDE_FROM_ALL
    ON
    DOWNLOAD_ONLY
    YES)

if(lua_ADDED)
    file(
        GLOB
        lua_sources
        CONFIGURE_DEPENDS
        ${lua_SOURCE_DIR}/*.c)
    list(
        REMOVE_ITEM
        lua_sources
        "${lua_SOURCE_DIR}/lua.c"
        "${lua_SOURCE_DIR}/luac.c"
        "${lua_SOURCE_DIR}/onelua.c")
    add_library(lua STATIC ${lua_sources})
    target_include_directories(lua PUBLIC $<BUILD_INTERFACE:${lua_SOURCE_DIR}>)
    # Build as C
    set_target_properties(lua PROPERTIES LINKER_LANGUAGE C)
endif()
