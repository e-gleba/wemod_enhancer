# Install the Python patcher CLI next to version.dll so the
# install tree is self-contained: users need only Python (no CMake)
# to run `python bin/wemod_enhancer.py patch --install-dir ...`.
# The package's __init__.py is fully self-contained (no relative
# imports), so it is installed renamed as a plain single-file
# script — the install tree stays free of package internals.
install(
    FILES "${CMAKE_CURRENT_SOURCE_DIR}/wemod_enhancer/__init__.py"
    DESTINATION "${CMAKE_INSTALL_BINDIR}"
    RENAME wemod_enhancer.py)
