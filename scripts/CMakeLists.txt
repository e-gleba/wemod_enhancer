# Patcher is part of every package: CLI zip is dll + py; GUI zip is
# gui + dll + py. Previously this returned early on GUI configures, so
# the script never landed next to the exe/elf.
install(PROGRAMS "${CMAKE_CURRENT_SOURCE_DIR}/wemod_enhancer.py"
        DESTINATION "${CMAKE_INSTALL_BINDIR}")
