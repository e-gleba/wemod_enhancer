# The patcher script + one-liner installers ship in every package
# next to the binaries.
install(PROGRAMS "${CMAKE_CURRENT_SOURCE_DIR}/wemod_enhancer.py"
        DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(PROGRAMS "${CMAKE_CURRENT_SOURCE_DIR}/install.sh"
                 "${CMAKE_CURRENT_SOURCE_DIR}/install.ps1"
        DESTINATION "${CMAKE_INSTALL_BINDIR}")
