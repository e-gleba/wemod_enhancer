# cmake_template

<p align="center">
  <img src=".github/logo.png" alt="cmake_template logo" width="200"/>
</p>

<p align="center">
  <a href="https://github.com/e-gleba/cmake_template/actions/workflows/cmake_multi_platform.yml"><img src="https://img.shields.io/github/actions/workflow/status/e-gleba/cmake_template/cmake_multi_platform.yml?branch=main&style=for-the-badge&labelColor=1C1C1C&logo=github&label=CI" alt="CI"/></a>
  <a href="https://isocpp.org/"><img src="https://img.shields.io/badge/C%2B%2B-23%2F26-00599C?style=for-the-badge&logo=cplusplus&logoColor=white&labelColor=1C1C1C" alt="C++ Standard"/></a>
  <a href="https://cmake.org"><img src="https://img.shields.io/badge/CMake-3.31%2B-064F8C?style=for-the-badge&logo=cmake&logoColor=white&labelColor=1C1C1C" alt="CMake"/></a>
  <a href="https://github.com/e-gleba/cmake_template/blob/main/license.md"><img src="https://img.shields.io/badge/License-MIT-blue?style=for-the-badge&labelColor=1C1C1C" alt="License"/></a>
  <a href="https://github.com/e-gleba/cmake_template/stargazers"><img src="https://img.shields.io/github/stars/e-gleba/cmake_template?style=for-the-badge&labelColor=1C1C1C&logo=github&color=F4C542" alt="Stars"/></a>
  <a href="docs/contributing.md"><img src="https://img.shields.io/badge/Contributing-Guide-4CAF50?style=for-the-badge&labelColor=1C1C1C" alt="Contributing Guide"/></a>
</p>

<p align="center">
  <b>The only CMake template with Android NDK, Linux→Windows cross-compilation, and Gradle Managed Devices — out of the box.</b>
</p>

<p align="center">
  <a href="https://github.com/e-gleba/cmake_template/generate"><img src="https://img.shields.io/badge/Use%20this%20template-%E2%9C%A8%20Generate%20new%20repo-238636?style=for-the-badge&logo=github&labelColor=1C1C1C" alt="Use this template"/></a>
</p>

---

Production-ready C++ template for cross-platform projects. Targets C++23/26. Ninja Multi-Config, CPM, clang-tidy, clang-format, pre-commit hooks. Packages with CPack. Tests with doctest — instrumented on Android via Gradle Managed Devices. Zero friction from clone to package on **Linux, Windows, Android, and macOS**.

```bash
# Desktop — full pipeline in one command
cmake --workflow --preset=gcc-full

# Android — configure + build for arm64
cmake --workflow --preset=android-arm64-full

# Linux → Windows ARM64 — cross-compile + package
cmake --workflow --preset=llvm-mingw-aarch64-full
```

---

## Why this template?

Most CMake starters stop at "it builds on my machine." This one ships to production.

| You need | Most templates | This template |
|---|---|---|
| **Android NDK** | ❌ Not even mentioned | ✅ 4 presets (arm64, arm32, x64, x86), API 24, `c++_shared`, Prefab |
| **Android instrumentation tests** | ❌ | ✅ `AndroidJUnitRunner`, Gradle Managed Devices (Pixel 6 ATD), doctest JNI bridge, [#23 native GTest strategy](https://github.com/e-gleba/cmake_template/issues/23) |
| **Linux → Windows cross-compile** | ❌ | ✅ llvm-mingw toolchain: x86_64, i686, aarch64 |
| **CMake Presets** | Basic or none | ✅ 10+ configure presets, 15+ build presets, workflow presets, schema v10 |
| **Packaging** | ❌ | ✅ CPack: tar.gz, zip, txz per platform |
| **Reproducible CI** | Manual Docker | ✅ Docker images + GitHub Actions matrix |
| **Code quality** | Maybe clang-format | ✅ clang-tidy, clang-format, `.cmake-format.yaml`, pre-commit hooks, `.editorconfig` |
| **C++ Standard** | 17 | ✅ **23 / 26** |

### Unique capabilities

- **Android NDK out of the box** — not just a toolchain file. A full Gradle project with `externalNativeBuild`, `prefab = true`, managed virtual devices, and `connectedCheck` test harness. Builds `.so` or `.aar`. See `android-project/`.
- **Cross-compile from Linux to Windows** — llvm-mingw toolchain included in `cmake/toolchains/`. Build `.exe` binaries for x86_64, i686, and aarch64 Windows — no Windows machine needed.
- **One-command workflows** — `cmake --workflow --preset=gcc-full` runs configure → build → test → package. No glue scripts.
- **Professional CMake architecture** — `CMAKE_CURRENT_LIST_DIR` everywhere (safe for `add_subdirectory`/`FetchContent`), cross-compilation-aware `CMAKE_FIND_ROOT_PATH`, build-type-safe flag management.
- **Honest roadmap** — every missing feature is a tracked issue, not a hidden gap. See [open issues](https://github.com/e-gleba/cmake_template/issues).

---

## Quick Start

### 1. Create your repo from this template

<p align="center">
  <a href="https://github.com/e-gleba/cmake_template/generate"><b>🎯 Click here: Use this template → Create new repository</b></a>
</p>

Then clone your new repo:

```bash
git clone https://github.com/YOUR_USER/YOUR_PROJECT
cd YOUR_PROJECT
```

### 2. Build and test (desktop)

```bash
# GCC — full pipeline
cmake --workflow --preset=gcc-full

# Or step by step:
cmake --preset=gcc
cmake --build --preset=gcc-release
ctest --preset=gcc-release

# Clang
cmake --workflow --preset=clang-full

# MSVC (Windows only)
cmake --workflow --preset=msvc-full
```

### 3. Cross-compile for Android

```bash
# Requires: ANDROID_NDK_HOME or ANDROID_HOME set
cmake --preset=android-arm64
cmake --build --preset=android-arm64

# Run instrumented tests on device/emulator
cd android-project
./gradlew pixel_6_aosp_atd_30DebugAndroidTest
./gradlew connectedCheck
```

### 4. Cross-compile for Windows (from Linux)

```bash
# Requires: llvm-mingw installed
cmake --workflow --preset=llvm-mingw-x86_64-full
# → build/llvm-mingw-x86_64/package/cxx_project-*.zip
```

---

## Platform Matrix

| Platform | Preset | Generator | Test runner | Package |
|---|---|---|---|---|
| **Linux (native)** | `gcc`, `clang` | Ninja Multi-Config | `ctest` | `.tar.gz` |
| **Windows (native)** | `msvc` | Visual Studio 17 2022 | `ctest` | `.zip` |
| **Windows (cross)** | `llvm-mingw-x86_64`, `llvm-mingw-i686`, `llvm-mingw-aarch64` | Ninja Multi-Config | — (cross-compiled) | `.tar.xz` |
| **Android arm64** | `android-arm64` | Ninja Multi-Config | `gradlew connectedCheck` | — |
| **Android arm32** | `android-arm32` | Ninja Multi-Config | `gradlew connectedCheck` | — |
| **Android x64** | `android-x64` | Ninja Multi-Config | `gradlew connectedCheck` | — |
| **Android x86** | `android-x86` | Ninja Multi-Config | `gradlew connectedCheck` | — |
| **macOS** | `clang` (native) | Ninja Multi-Config | `ctest` | `.tar.gz` |
| **iOS / macOS Xcode** | [planned #20](https://github.com/e-gleba/cmake_template/issues/20) | Xcode | `xctest` / `xcodebuild test` | — |
| **WebAssembly** | [planned #2](https://github.com/e-gleba/cmake_template/issues/2) | Emscripten | — | — |

---

## Comparison: Competitive Landscape

| Feature | **cmake_template** | [cpp-best-practices](https://github.com/cpp-best-practices/cmake_template) | [modern-cpp-template](https://github.com/filipdutescu/modern-cpp-template) | [cmake-init](https://github.com/cginternals/cmake-init) |
|---|---|---|---|---|
| **C++ Standard** | **23 / 26** | 17 / 20 | 17 | 11+ |
| **CMake Presets** | ✅ 10+ with workflows | ❌ | ❌ | ❌ |
| **Android NDK** | ✅ 4 ABI | ❌ | ❌ | ❌ |
| **Android instrumented tests** | ✅ GMD + doctest JNI | ❌ | ❌ | ❌ |
| **Linux → Windows cross** | ✅ llvm-mingw (3 arch) | ❌ | ❌ | ❌ |
| **WebAssembly** | ❌ [#2](https://github.com/e-gleba/cmake_template/issues/2) | ✅ + Pages deploy | ❌ | ❌ |
| **Docker / CI** | ✅ + Actions matrix | ✅ Docker + Actions | ✅ GitHub Actions | ✅ |
| **CPack packaging** | ✅ tar.gz / zip / txz | ❌ | ❌ | ❌ |
| **Sanitizers** | ❌ [#9](https://github.com/e-gleba/cmake_template/issues/9) | ✅ ASan/UBSan | ✅ | ❌ |
| **Fuzz testing** | ❌ | ✅ libFuzzer | ❌ | ❌ |
| **Code coverage** | ❌ [#10](https://github.com/e-gleba/cmake_template/issues/10) | ✅ codecov | ✅ codecov | ❌ |
| **macOS/iOS (Xcode)** | ❌ [#20](https://github.com/e-gleba/cmake_template/issues/20) | Limited | ❌ | ❌ |
| **vcpkg** | ❌ [#3](https://github.com/e-gleba/cmake_template/issues/3) | ❌ | ❌ | ❌ |
| **GitHub Stars** | ⭐ *you are here* | 1,700+ | 1,900+ | 900+ |
| **Age** | ~1 year | 3 years | 5 years | 11 years |
| **License** | MIT | Unlicense | Unlicense | MIT |

> **Honest assessment:** This template leads in cross-compilation engineering (Android NDK, Linux→Win, Presets, CPack). It trails in sanitizers, fuzz testing, and code coverage — all actively tracked in the [roadmap](https://github.com/e-gleba/cmake_template/issues). It does not include Qt, OpenGL, or JUCE scaffolding — those are well covered by specialized templates.

---

## Project Structure

```
.
├── CMakeLists.txt           # Root: project(), CPM, code quality, CTest, CPack
├── CMakePresets.json        # 10+ configure, 15+ build, workflow presets
├── cmake/
│   ├── toolchains/          # llvm-mingw.cmake, future ios.cmake
│   ├── code_quality/        # clang-tidy, clang-format, cpplint configs
│   └── description/         # package_description.cmake
├── src/                     # Library sources
│   └── CMakeLists.txt
├── tests/                   # doctest-based tests
│   ├── CMakeLists.txt       # Shared lib on Android, executable on desktop
│   ├── doctest_android_jni.cpp  # JNI bridge for Android instrumentation
│   └── doctest_example.cpp
├── android-project/         # Gradle project with AGP, GMD, AndroidJUnitRunner
│   ├── app/
│   │   ├── build.gradle     # externalNativeBuild → ../../CMakeLists.txt
│   │   └── src/
│   │       ├── main/        # NativeActivity entry point
│   │       └── androidTest/ # Instrumented test wrappers
│   ├── build.gradle
│   └── settings.gradle
├── docker/                  # Dockerfiles for CI reproducibility
├── scripts/                 # Build helpers
├── docs/                    # Architecture, presets, Docker guides
└── tools/                   # IWYU mappings, code quality configs
```

---

## Documentation

| Document | What it covers |
|---|---|
| [Presets & Platforms](docs/presets.md) | All CMake presets, platform support, cross-compilation details |
| [Architecture](docs/architecture.md) | CMake design decisions, directory structure, `PROJECT_IS_TOP_LEVEL` pattern |
| [Docker Guide](docs/docker.md) | Docker images for CI, local reproducible builds |
| [Contributing](docs/contributing.md) | How to contribute, code style, pre-commit setup |
| [References](docs/references.md) | Professional CMake, modern CMake, toolchain references |
| [Issue: Android native testing strategy](https://github.com/e-gleba/cmake_template/issues/23) | GTest vs doctest, Activity lifecycle, XCTest, CI/CD — research-backed |

---

## Consulting

I help teams reduce C++ build friction and ship cross-platform products faster.

**Services:**
- CMake architecture audits and modernisation
- Android NDK toolchain setup and Gradle integration
- CI/CD pipeline design for C++ (GitHub Actions, GitLab CI, Docker)
- Cross-compilation pipelines (Linux→Windows, Linux→Android, macOS→iOS)
- CPack packaging and distribution
- Team onboarding workshops

**Why work with me:**
- This template is the public portfolio — it demonstrates real engineering depth in CMake, NDK, cross-compilation, and CI
- I focus exclusively on **build engineering** — not general software consulting
- Every engagement starts with a concrete deliverable, not a roadmap document

<p align="center">

| | |
|---|---|
| 🌐 **Website** | [e-gleba.github.io](https://e-gleba.github.io) |
| 📧 **Email** | [i@egleba.ru](mailto:i@egleba.ru) — fastest response |
| 💬 **Discussions** | [GitHub Discussions](https://github.com/e-gleba/cmake_template/discussions) |
| 💼 **Rate** | Up to $150/hr depending on scope |
| 📍 **Location** | Remote-first, worldwide |

</p>

---

## Roadmap

All planned features are tracked as GitHub issues with the `enhancement` label.

| Priority | Issue | Feature |
|---|---|---|
| 🔴 P0 | [#20](https://github.com/e-gleba/cmake_template/issues/20) | macOS/iOS Xcode presets + XCTest |
| 🔴 P0 | [#9](https://github.com/e-gleba/cmake_template/issues/9) | Sanitizers (ASan/UBSan/TSan) |
| 🟡 P1 | [#10](https://github.com/e-gleba/cmake_template/issues/10) | Code coverage (codecov/CodeQL) |
| 🟡 P1 | [#2](https://github.com/e-gleba/cmake_template/issues/2) | WebAssembly (Emscripten) |
| 🟡 P1 | [#3](https://github.com/e-gleba/cmake_template/issues/3) | vcpkg compatibility |
| 🟢 P2 | [#5](https://github.com/e-gleba/cmake_template/issues/5) | C++20 modules |
| 🟢 P2 | [#11](https://github.com/e-gleba/cmake_template/issues/11) | Steam Runtime / Steam Deck |
| 🟢 P2 | [#8](https://github.com/e-gleba/cmake_template/issues/8) | Prebuilt / air-gapped dependency mode |

[View all issues →](https://github.com/e-gleba/cmake_template/issues)

---

## License

MIT — see [license.md](license.md).

This is not a "source available" or dual-licensed project. Use it for anything — commercial products, internal tools, or as a base for your own template. Attribution appreciated but not required.
