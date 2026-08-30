if(NOT WEMOD_ENHANCER_BUILD_GUI)
    return()
endif()

install(PROGRAMS "${CMAKE_CURRENT_SOURCE_DIR}/wemod_enhancer.py"
        DESTINATION "${CMAKE_INSTALL_BINDIR}")
