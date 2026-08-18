# Install the Python patcher package next to version.dll so the
# install tree is self-contained: users need only Python (no CMake)
# to run `python bin/wemod_enhancer patch --install-dir ...` — the
# package directory executes directly via its __main__.py.
install(
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/wemod_enhancer"
    DESTINATION "${CMAKE_INSTALL_BINDIR}"
    PATTERN "__pycache__" EXCLUDED)
