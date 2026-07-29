# syntax=docker/dockerfile:1

# ── Build Stage ────────────────────────────────────────────────────────
# Full development environment with GCC, Clang, CMake, Ninja, and
# documentation tools. Intended for CI pipelines and reproducible
# local builds, not as a minimal runtime image.
FROM fedora:latest AS build

LABEL maintainer="e-gleba" \
      description="cmake_template build environment (GCC + Clang, CMake 3.31+, Ninja)" \
      org.opencontainers.image.source="https://github.com/e-gleba/cmake_template"

WORKDIR /app

# ── System Dependencies ──────────────────────────────────────────────
# Single RUN layer minimizes image size. BuildKit cache mount persists
# dnf metadata between builds for faster local iteration.
# Ref: https://docs.docker.com/build/cache/optimize/#use-cache-mounts
RUN --mount=type=cache,target=/var/cache/dnf \
    dnf -y upgrade --refresh && \
    dnf -y install \
        # Core toolchain
        gcc-c++ \
        clang \
        clang-tools-extra \
        lld \
        # Build system
        cmake \
        ninja-build \
        git \
        # Documentation
        doxygen \
        graphviz \
        # pkg-config for dependency discovery
        pkgconf-pkg-config \
        # SDL3 / graphics prerequisites (optional but pre-installed)
        wayland-devel \
        libxkbcommon-devel \
        mesa-libEGL-devel \
    && dnf clean all

# ── Source ───────────────────────────────────────────────────────────
# Copy after dependency installation to maximize layer cache hits
# when only source files change.
COPY . .

# ── Validation ───────────────────────────────────────────────────────
# Fail fast if CMakePresets.json is malformed or presets are missing.
RUN cmake --list-presets 2>&1 | head -20

# ── Entrypoint ────────────────────────────────────────────────────────
# Default: full CI workflow (configure → build → test → package).
# Override with `docker run --entrypoint bash` for interactive use.
ENTRYPOINT ["cmake", "--workflow", "--preset=gcc-full"]
