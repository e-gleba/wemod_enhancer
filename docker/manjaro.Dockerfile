# syntax=docker/dockerfile:1

# ── Build Stage ────────────────────────────────────────────────────────
# Rolling-release validation image based on Manjaro (Arch Linux).
# Provides bleeding-edge compiler versions for testing compatibility
# with latest GCC/Clang releases before they reach stable distros.
FROM manjarolinux/base:latest AS build

LABEL maintainer="e-gleba" \
      description="cmake_template build environment on Manjaro (rolling release)" \
      org.opencontainers.image.source="https://github.com/e-gleba/cmake_template"

WORKDIR /app

# ── System Dependencies ──────────────────────────────────────────────
# base-devel provides gcc, make, glibc headers (pthread.h, dlfcn.h).
# BuildKit cache mount persists pacman packages between builds.
# Ref: https://docs.docker.com/build/cache/optimize/#use-cache-mounts
RUN --mount=type=cache,target=/var/cache/pacman/pkg \
    pacman -Syu --noconfirm && \
    pacman -S --needed --noconfirm \
        base-devel \
        clang \
        lld \
        cmake \
        ninja \
        git \
        doxygen \
        graphviz \
        pkgconf \
        # SDL3 / graphics prerequisites (optional but pre-installed)
        wayland \
        libxkbcommon \
        mesa \
    && yes | pacman -Scc

# ── Source ─────────────────────────────────────────────────────────--
# Copy after dependency installation to maximize layer cache hits.
COPY . .

# ── Validation ────────────────────────────────────────────────────────
# Fail fast if CMakePresets.json is malformed or presets are missing.
RUN cmake --list-presets 2>&1 | head -20

# ── Entrypoint ────────────────────────────────────────────────────────
# Default: full CI workflow (configure → build → test → package).
# Override with `docker run --entrypoint bash` for interactive use.
ENTRYPOINT ["cmake", "--workflow", "--preset=gcc-full"]
