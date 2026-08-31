# The patcher script ships in every package next to the binaries.
install(PROGRAMS "${CMAKE_CURRENT_SOURCE_DIR}/wemod_enhancer.py"
        DESTINATION "${CMAKE_INSTALL_BINDIR}")
