# Docker Guide

**Reproducible, portable build environments for the cmake_template.**

Docker images encapsulate the exact toolchain versions (CMake 3.31+, GCC/Clang, Ninja, Doxygen) needed to build, test, and package this project consistently across Linux distributions, CI runners, and developer machines. The goal is **build-once, run-anywhere** — no "works on my machine" due to distro-specific package versions.

## Available Images

| Image | Base | Use Case | Size (approx.) |
|-------|------|----------|----------------|
| `fedora` | `fedora:latest` | Primary CI / development image; most stable toolchain | ~500 MB |
| `manjaro` | `manjarolinux/base:latest` | Rolling-release validation; bleeding-edge compiler versions | ~450 MB |

> **Note:** These images target **Linux native builds** (`gcc`, `clang`, `llvm-mingw-x86_64` cross). Android NDK cross-compilation uses the host's NDK installation or a dedicated Android builder image (see [Presets](presets.md)). macOS/iOS builds require macOS hosts and are not containerized.

## Quick Start

### Build

```bash
# Fedora (recommended for CI parity)
docker build -t cmake-template:fedora -f docker/fedora.Dockerfile .

# Manjaro (rolling release validation)
docker build -t cmake-template:manjaro -f docker/manjaro.Dockerfile .
```

### Run Full Workflow

The default `ENTRYPOINT` runs the complete CI pipeline:

```bash
docker run --rm -v "$(pwd):/app" cmake-template:fedora
```

This executes `cmake --workflow --preset=gcc-full` inside the container, producing build artifacts, test results, and CPack packages in your local `build/` and `dist/` directories.

### Interactive Development

```bash
docker run --rm -it -v "$(pwd):/app" cmake-template:fedora bash
# Inside container:
cmake --preset=clang
cmake --build --preset=clang-release
ctest --preset=clang-release
```

### Cross-Compilation (Linux → Windows)

The Fedora image includes `llvm-mingw` dependencies. Mount your llvm-mingw toolchain or install it in the container:

```bash
docker run --rm -it -v "$(pwd):/app" -v "/path/to/llvm-mingw:/opt/llvm-mingw" cmake-template:fedora bash
export PATH="/opt/llvm-mingw/bin:$PATH"
cmake --workflow --preset=llvm-mingw-x86_64-full
```

## Architecture

### Multi-Stage Design

Both Dockerfiles use a single `build` stage strategy for simplicity. The images are **not minimal runtime containers** — they are full development environments. A future `runtime` stage stripping build-only dependencies (compiler headers, static analysis tools) is tracked as an enhancement.

Key design decisions:

- **BuildKit cache mounts** (`--mount=type=cache`) keep package manager caches (`dnf`/`pacman`) between builds, dramatically speeding up local iteration.
- **Single `RUN` layer for system deps** minimizes image layers and reduces final size.
- **`COPY . .` comes after dependency installation** maximizes layer cache hits when only source code changes.
- **`ENTRYPOINT` defaults to `gcc-full` workflow** makes `docker run` a one-command CI equivalent.

### Layer Strategy

```
Layer 1: Base image (fedora:latest / manjarolinux/base:latest)
Layer 2: System dependencies (cached across source changes)
Layer 3: Source code copy (invalidated on any source change)
Layer 4: Preset validation (fail-fast if CMakePresets.json is broken)
Layer 5: Entrypoint (workflow execution)
```

## Image Variants and Tagging Convention

For CI and reproducibility, tag images explicitly:

```bash
# Date-stamped tag for CI cache keys
docker build -t cmake-template:fedora-$(date +%Y%m%d) -f docker/fedora.Dockerfile .
# Toolchain-pinned tag
docker build -t cmake-template:fedora-gcc14 -f docker/fedora.Dockerfile .
```

## CI Integration

### GitHub Actions

```yaml
jobs:
  docker-build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Build and run in Docker
        run: |
          docker build -t cmake-template -f docker/fedora.Dockerfile .
          docker run --rm -v "$(pwd):/app" cmake-template
      - name: Upload artifacts
        uses: actions/upload-artifact@v4
        with:
          name: packages
          path: build/**/packages/*
```

### GitLab CI

```yaml
.docker-build:
  image: docker:latest
  services:
    - docker:dind
  script:
    - docker build -t $CI_PROJECT_NAME -f docker/fedora.Dockerfile .
    - docker run --rm -v "$CI_PROJECT_DIR:/app" $CI_PROJECT_NAME
  artifacts:
    paths:
      - build/**/packages/
```

## Troubleshooting

| Symptom | Cause | Resolution |
|---------|-------|------------|
| `cmake: command not found` | Base image drifted, package missing | Rebuild with `--no-cache` |
| Slow rebuilds | Package cache not mounted | Ensure BuildKit is enabled (`DOCKER_BUILDKIT=1`) |
| Permission errors on `build/` | Container user ≠ host user | Run with `--user $(id -u):$(id -g)` or fix ownership post-build |
| NDK not found in Android preset | NDK not mounted inside container | Android cross-compilation requires host NDK or separate Android builder image |
| `llvm-mingw` preset fails | Cross-compiler not in container PATH | Mount toolchain volume or extend Dockerfile to install it |

## Extending the Images

To add a new base distribution (e.g., Ubuntu LTS, Alpine, Arch):

1. Copy `docker/fedora.Dockerfile` as a template.
2. Replace `dnf` commands with the target package manager (`apt`, `apk`, `pacman`).
3. Ensure these packages are present: `cmake >= 3.31`, `ninja`, `gcc/g++` or `clang/clang++`, `git`, `doxygen`.
4. Add optional: `llvm-mingw` packages or mount for Windows cross-compilation.
5. Update this document and the [Presets guide](presets.md).

## Further Reading

- [Architecture](architecture.md) — project structure and build system design
- [Presets, Platforms & Cross-Compilation](presets.md) — all available configure/build/test/package presets
- [Contributing](contributing.md) — guidelines for modifying Dockerfiles
- [Docker BuildKit docs](https://docs.docker.com/build/buildkit/) — cache mounts, multi-platform builds
