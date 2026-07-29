
## Why this template?

Most CMake starters stop at "it builds on my machine". This template goes further with first-class cross-compilation and packaging.

- **Android NDK out of the box** — 4 presets (arm64, arm32, x64, x86) with API 24.
- **Linux → Windows cross-compile** — 3 llvm-mingw presets (x86_64, i686, aarch64).
- **Reproducible builds** — Docker images for CI and local development.
- **One-command pipelines** — `cmake --workflow` handles configure → build → test → package.
- **Modern standards** — C++23/26 with clang-tidy, clang-format, IWYU-ready structure.

## Prerequisites

```
cmake 3.31+
ninja 1.11+
C++23-capable compiler (clang 16+, gcc 13+, msvc 19.35+)
```

### macOS

```bash
brew install cmake llvm ninja doxygen
```

### Linux (Fedora)

```bash
sudo dnf install cmake gcc-c++ ninja-build doxygen llvm clang-tools-extra
```

### Linux (Ubuntu/Debian)

```bash
sudo apt install cmake g++ ninja-build doxygen llvm clang-tools
```

### Windows

```powershell
choco install cmake llvm ninja doxygen visualstudio2022buildtools
```

## Project Structure

```
.
├── CMakeLists.txt
├── CMakePresets.json
├── cmake/                  # find_package modules (warnings, cpm, code quality)
├── src/                    # application sources
├── tests/                  # doctest + CTest
├── tools/                  # helper scripts
├── docker/                 # Dockerfiles for reproducible builds
├── android-project/        # Android project scaffolding
├── .clang-format           # clang-format config
├── .clang-tidy             # clang-tidy config
├── .cmake-format.yaml      # cmake-format config
├── .editorconfig           # editor defaults
├── .pre-commit-config.yaml # pre-commit hooks
└── license                 # MIT
```

## Code Quality

Format sources and CMake files:

```bash
cmake --build build/gcc --target format
```

Static analysis:

```bash
cmake --build build/gcc --target tidy
```

Lint:

```bash
cmake --build build/gcc --target cpplint
```

Pre-commit hooks enforce formatting on every commit. Install once:

```bash
pre-commit install
```

## Documentation

```bash
cmake --build build/gcc --target doxygen
```

Output: `build/gcc/docs/doxygen/html`.

## Dependencies

Managed via [CPM](https://github.com/cpm-cmake/CPM.cmake). Add packages in CMakeLists.txt:

```cmake
CPMAddPackage("gh:fmtlib/fmt#11.1.4")
```

CPM downloads are verbose (`FETCHCONTENT_QUIET=OFF`) for CI visibility.

## IDE Support

The project generates `compile_commands.json` and is compatible with CLion, Visual Studio, QtCreator, KDevelop, and any LSP-based editor.
