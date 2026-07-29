# Pull Request

## Summary
<!-- One sentence: what does this PR do? -->

## Related Issue
<!-- Link: Fixes #123, Closes #456, or "No issue — docs fix." -->

## Changes
<!-- Surgical list: files touched, behavior changed. No fluff. -->

## Verification
<!-- Checklist — tick what you actually ran -->
- [ ] `cmake --preset=gcc && cmake --build --preset=gcc-release && ctest --preset=gcc-release` passes
- [ ] `cmake --workflow --preset=gcc-full` passes (if affecting packaging/presets)
- [ ] `pre-commit run --all-files` passes (formatting, lint)
- [ ] Affected cross-compilation presets tested (if applicable: `llvm-mingw-*`, `android-*`)
- [ ] Documentation updated (`docs/*.md`, `README.md` comparison table if adding features)
- [ ] `docker build` succeeds if Dockerfiles changed

## Notes for Reviewer
<!-- Anything non-obvious? Breaking changes? Open questions? -->
