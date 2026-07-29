# Contributing to cmake_template

Thank you for your interest in contributing to this production-ready CMake template. We aim for **enterprise-grade quality**, clear documentation, and maintainable cross-platform support.

## Code of Conduct

This project follows the [Contributor Covenant Code of Conduct (v2.1)](https://www.contributor-covenant.org/version/2/1/code_of_conduct/). Participation requires upholding these standards. Report unacceptable behavior to `i@egleba.ru`.

## How to Contribute

### 1. Reporting Issues
- Use GitHub issue templates when available.
- Include:
  - Exact preset used (`--preset=xxx`)
  - Host OS, CMake version, compiler version
  - Reproduction steps
  - Full error logs
  - Expected behavior

Particularly welcome: issues with Android NDK, llvm-mingw cross-compilation, CPack, or Docker reproducibility.

### 2. Feature Requests & Roadmap
See open issues, especially:
- [#20 - macOS and iOS support with Xcode generator](https://github.com/e-gleba/cmake_template/issues/20)
- Sanitizers, WASM/Emscripten, vcpkg integration, Steam Runtime, prebuilt deps, etc.

Discuss major changes in an issue or Discussion first.

### 3. Pull Requests

**Follow these steps for smooth merging:**

1. Fork the repository and create a branch with a descriptive name:
   ```bash
   git checkout -b docs/improve-contributing or feat/add-macos-preset
   ```
2. Make **surgical changes** only (see [Behavioral Guidelines](CLAUDE.md)). Do not refactor unrelated code.
3. Test thoroughly using the provided presets and workflows:
   - `cmake --preset=gcc && cmake --build --preset=gcc-release && ctest --preset=gcc-release`
   - Test affected platforms (Android, cross-compile)
   - Run `cmake --workflow --preset=gcc-full` where applicable
4. Update documentation (README.md, docs/presets.md, this file) if behavior or usage changes.
5. Ensure pre-commit hooks pass (`pre-commit run --all-files`).
6. Commit using clear, conventional messages.
7. Open the PR with:
   - Clear title and description
   - Link to related issue
   - Checklist of what was tested

The maintainer will focus review on:
- Cross-platform compatibility preservation
- Documentation completeness
- Adherence to simplicity and "surgical changes" principles
- Professional tone in docs

## Development Environment

Use the template's own presets — this is self-hosting.

```bash
# Quick setup
cmake --preset=gcc
cmake --build --preset=gcc --target format tidy
pre-commit install
```

**Recommended tools** (see docs/presets.md and architecture.md for details):
- CMake 3.31+
- Ninja
- clang-format, clang-tidy
- pre-commit
- Docker for reproducible CI-like builds

## Standards

- **CMake**: Modern, target-based. Prefer presets over cache variables in docs. Document toolchain specifics.
- **C++**: Target C++23 with forward compatibility for 26. Follow .clang-tidy and .clang-format strictly.
- **Documentation**: Professional, comprehensive, scannable. Use tables, code blocks, TOCs, consistent tone. Update comparison table in README.md for new features.
- **Presets & Platforms**: New additions must be documented in the merged `docs/presets.md`. Support debug/release variants. Demonstrate cross-compilation where applicable.
- **Merging Presets/Platforms**: Documentation has been consolidated into a single professional guide explaining how presets abstract platform toolchains for seamless native and cross builds.

## Adding New Platforms (e.g. macOS/iOS)

See [#20](https://github.com/e-gleba/cmake_template/issues/20) for detailed spec.

General process:
1. Add configure/build/test/package presets to `CMakePresets.json` (use inheritance, conditions for OS detection where possible).
2. Update `docs/presets.md` with usage, prerequisites (Xcode CLI, full Xcode for iOS), limitations.
3. Add or update CI workflow for `macos-latest` runner.
4. Update README comparison table and roadmap.
5. Provide sample for common Apple use cases (code signing variables, universal binaries, Xcode project generation).

Xcode generator is preferred for native macOS/iOS to enable full IDE integration, schemes, and asset management.

## Recognition

Contributors are credited in git history. Significant or repeated contributions may be highlighted in the README "Thanks" section, release notes, or a future CONTRIBUTORS file.

---

**This template is also a portfolio piece demonstrating professional open-source practices.** High-quality PRs help both the project and your visibility.

Questions? Open a [Discussion](https://github.com/e-gleba/cmake_template/discussions) or contact via the consulting section in the README.
