# AI Coding Guidelines for cmake_template

This file helps AI assistants (Claude, Cursor, Copilot) understand the project conventions and produce code that matches the repository's standards.

## Project Overview

- **Type**: Production-ready C++ project template / skeleton
- **Standard**: C++23/26
- **CMake**: 3.31+
- **Generator**: Ninja Multi-Config (except MSVC preset)
- **Package Manager**: CPM.cmake (CMake-based, no external package manager like Conan/vcpkg yet)
- **Test Framework**: doctest + CTest presets
- **CI**: GitHub Actions (`cmake_multi_platform.yml`)
- **Docker**: Fedora-based reproducible build images

## Directory Layout Rules

```
.
├── CMakeLists.txt              # Root: project(), CPM init, subdirs
├── CMakePresets.json           # Single source of truth for all presets
├── cmake/
│   ├── warnings.cmake          # Compiler warning flags per toolchain
│   ├── cpm.cmake               # CPM bootstrap
│   └── code_quality.cmake      # clang-tidy, IWYU, cppcheck targets
├── src/                        # Application/library sources
│   └── CMakeLists.txt
├── tests/                      # doctest-based tests, CTest-ready
│   └── CMakeLists.txt
├── tools/                      # Helper scripts (format, lint, etc.)
├── docker/
│   └── fedora.Dockerfile       # Primary reproducible image
├── android-project/            # Android manifest scaffolding
├── docs/
│   └── references.md           # Curated external links (do NOT bloat README)
├── .clang-format
├── .clang-tidy
├── .cmake-format.yaml
├── .editorconfig
└── .pre-commit-config.yaml
```

**Rule**: Never add ad-hoc `build/` directories to `.gitignore` — the project already ignores common build roots. Always use `build/<preset>/` or out-of-tree.

## Adding a New CMake Preset

1. Edit `CMakePresets.json` only.
2. Follow existing naming: `<compiler>-<variant>` for configure, `<name>-release/debug` for build, `<name>-package` for CPack, `<name>-full` for workflow.
3. Every native preset must have matching `build`, `test` (if native), and optionally `package`/`workflow` entries.
4. Cross-compiled presets (Android, MinGW) disable `test` — packaging or manual device/emulator testing takes over.
5. Keep `vendor` block minimal; do not add IDE-specific fields unless requested.

## Adding Dependencies

Use CPM only. Example:

```cmake
CPMAddPackage("gh:fmtlib/fmt#11.1.4")
```

- Prefer tagged releases over branches.
- Keep `FETCHCONTENT_QUIET OFF` for CI visibility (already set globally).
- If a library needs options, pass `OPTIONS "LIB_BUILD_TESTS OFF"` inside `CPMAddPackage`.
- Do NOT add `find_package` / `conanfile.txt` / `vcpkg.json` — these are tracked in separate issues (#3, #8).

## Writing Tests

- Use **doctest** (already fetched via CPM).
- Place test files under `tests/`.
- Register with `doctest_discover_tests(...)` in `tests/CMakeLists.txt`.
- CTest presets mirror build presets: `gcc-release`, `clang-debug`, etc.
- For Android instrumentation tests, use the Android project scaffolding in `android-project/` and document the emulator/device target in the issue/PR.

## Code Quality (Mandatory Before Commit)

```bash
cmake --build build/gcc --target format   # clang-format + cmake-format
cmake --build build/gcc --target tidy     # clang-tidy
cmake --build build/gcc --target cpplint  # cppcheck/cpplint
```

- All source files must pass `.clang-format`.
- CMake files must pass `.cmake-format.yaml`.
- Pre-commit hooks enforce this; run `pre-commit install` once.

## CI / GitHub Actions Rules

- The main workflow is `.github/workflows/cmake_multi_platform.yml`.
- New matrix entries must use existing presets from `CMakePresets.json`.
- Do NOT add platform-specific hack scripts in CI — encode logic in presets or Docker.
- Docker builds must be reproducible: pin Fedora/LLVM/CMake versions in `docker/fedora.Dockerfile`.

## Cross-Compilation Conventions

- **Android**: Use NDK toolchain via CMake `CMAKE_TOOLCHAIN_FILE`. Preset names prefix with `android-`.
- **Linux → Windows**: Use `llvm-mingw` toolchain. Preset names prefix with `llvm-mingw-`.
- Toolchain files live under `cmake/toolchains/` if needed (create the dir only when adding custom toolchains).
- Always document host dependencies (e.g., `llvm-mingw` on `PATH`) in the preset `description` field.

## README Maintenance

- Keep `readme.md` under 10 KB.
- Quick Start must be ≤ 3 commands.
- Move curated links, deep references, and long essays to `docs/references.md`.
- Comparison table is the only allowed long table in README.
- Consulting block stays at the bottom; do not add sponsor badges (region-blocked).

## Commit Message Style

Follow conventional commits:

```
feat(ci): add emscripten preset
fix(docker): pin ninja version
chore(docs): update comparison table
deps: bump doctest to 2.4.11
```

## Questions?

If unsure, open a Discussion or check existing issue research blocks before writing new code.

# CLAUDE.md core

Behavioral guidelines to reduce common LLM coding mistakes. Merge with project-specific instructions as needed.

**Tradeoff:** These guidelines bias toward caution over speed. For trivial tasks, use judgment.

## 1. Think Before Coding

**Don't assume. Don't hide confusion. Surface tradeoffs.**

Before implementing:
- State your assumptions explicitly. If uncertain, ask.
- If multiple interpretations exist, present them - don't pick silently.
- If a simpler approach exists, say so. Push back when warranted.
- If something is unclear, stop. Name what's confusing. Ask.

## 2. Simplicity First

**Minimum code that solves the problem. Nothing speculative.**

- No features beyond what was asked.
- No abstractions for single-use code.
- No "flexibility" or "configurability" that wasn't requested.
- No error handling for impossible scenarios.
- If you write 200 lines and it could be 50, rewrite it.

Ask yourself: "Would a senior engineer say this is overcomplicated?" If yes, simplify.

## 3. Surgical Changes

**Touch only what you must. Clean up only your own mess.**

When editing existing code:
- Don't "improve" adjacent code, comments, or formatting.
- Don't refactor things that aren't broken.
- Match existing style, even if you'd do it differently.
- If you notice unrelated dead code, mention it - don't delete it.

When your changes create orphans:
- Remove imports/variables/functions that YOUR changes made unused.
- Don't remove pre-existing dead code unless asked.

The test: Every changed line should trace directly to the user's request.

## 4. Goal-Driven Execution

**Define success criteria. Loop until verified.**

Transform tasks into verifiable goals:
- "Add validation" → "Write tests for invalid inputs, then make them pass"
- "Fix the bug" → "Write a test that reproduces it, then make it pass"
- "Refactor X" → "Ensure tests pass before and after"

For multi-step tasks, state a brief plan:
```
1. [Step] → verify: [check]
2. [Step] → verify: [check]
3. [Step] → verify: [check]
```

Strong success criteria let you loop independently. Weak criteria ("make it work") require constant clarification.

---

**These guidelines are working if:** fewer unnecessary changes in diffs, fewer rewrites due to overcomplication, and clarifying questions come before implementation rather than after mistakes.
