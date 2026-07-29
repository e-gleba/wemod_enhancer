cmake_minimum_required(VERSION 3.31)

project(
    wemod_enhancer
    VERSION 1.0.0
    DESCRIPTION "Cross-platform Wand/WeMod Electron patcher and Windows ASAR fuse proxy"
    HOMEPAGE_URL "https://github.com/e-gleba/wemod_enhancer"
    LANGUAGES C)

add_subdirectory(src)

if(PROJECT_IS_TOP_LEVEL)
    set(CPACK_PACKAGE_NAME "wemod-enhancer")
    set(CPACK_PACKAGE_VENDOR "e-gleba")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Wand/WeMod Electron proxy DLL")
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
    set(CPACK_PACKAGE_FILE_NAME
        "${CPACK_PACKAGE_NAME}-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
    include(CPack)
endif()
