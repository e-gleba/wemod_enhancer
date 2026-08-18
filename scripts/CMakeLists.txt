# Install the Python patcher CLI next to version.dll so the
# install tree is self-contained: users need only Python (no CMake)
# to run `python wemod_enhancer.py patch --install-dir ...`.
install(
    PROGRAMS "${CMAKE_CURRENT_SOURCE_DIR}/wemod_enhancer.py"
    DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
