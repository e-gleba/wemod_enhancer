# Docker Build Environment Reference

This directory contains **reproducible containerized build environments** for the cmake_template project.

## Files

| File | Purpose |
|------|---------|
| `fedora.Dockerfile` | Primary CI/development image. Stable toolchain, well-tested. Recommended default. |
| `manjaro.Dockerfile` | Rolling-release validation. Tests compatibility with latest compiler versions. |
| `.dockerignore` | Excludes build artifacts, editor files, and large directories from the build context. |

## Usage

For comprehensive usage instructions (build commands, CI integration, troubleshooting, architecture rationale), see the project's main Docker documentation at [`docs/docker.md`](../docs/docker.md).

## Quick Commands

```bash
# Build Fedora image
docker build -t cmake-template:fedora -f fedora.Dockerfile ..

# Build Manjaro image
docker build -t cmake-template:manjaro -f manjaro.Dockerfile ..

# Run full workflow (mount project root)
docker run --rm -v "$(pwd)/..:/app" cmake-template:fedora

# Interactive shell
docker run --rm -it -v "$(pwd)/..:/app" --entrypoint bash cmake-template:fedora
```

> All images require BuildKit (`DOCKER_BUILDKIT=1`) for cache mount support.

## Design Notes

- Both Dockerfiles use a **single-stage build** strategy. They are full development environments, not minimal runtime containers.
- **BuildKit cache mounts** (`--mount=type=cache`) accelerate local rebuilds by persisting package manager caches.
- `COPY` happens after system dependency installation to maximize Docker layer cache efficiency.
- `ENTRYPOINT` defaults to `cmake --workflow --preset=gcc-full` for one-command CI equivalence.

## Contributing

When modifying these files:

1. Ensure `cmake --list-presets` validation still passes.
2. Verify the image builds with `docker build --no-cache`.
3. Test the full workflow: `docker run --rm -v "$(pwd)/..:/app" <image>`.
4. Update `docs/docker.md` if behavior or available images change.

See [`docs/contributing.md`](../docs/contributing.md) for project-wide contribution guidelines.
