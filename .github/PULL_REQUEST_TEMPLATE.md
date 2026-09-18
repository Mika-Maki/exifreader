<!-- Thanks for contributing to exifreader! Please fill in the sections below. -->

## Summary

<!-- What does this change do, and why? One or two sentences. -->

## Related issue

<!-- "Fixes #123" or "Refs #456". Delete if not applicable. -->

## Type of change

- [ ] Bug fix (non-breaking)
- [ ] New feature (non-breaking)
- [ ] Breaking change (output, CLI, or exit-code change)
- [ ] Refactor or performance work (no behavior change)
- [ ] Build, CI or documentation only

## Checklist

- [ ] I have read [CONTRIBUTING.md](CONTRIBUTING.md).
- [ ] `make test` passes locally.
- [ ] The files I touched are clang-format clean (`make format`).
- [ ] The build is warning-clean with `-Wall -Wextra -Wpedantic -Wshadow -Wconversion`.
- [ ] New or changed behavior has a fixture in `tests/make_fixtures.py` and an
      assertion in `tests/run_tests.sh`.
- [ ] Any new file read goes through `ByteReader` or an explicit range check.
- [ ] I added a `## [Unreleased]` entry to [CHANGELOG.md](CHANGELOG.md) for
      user-visible changes.
- [ ] This is not a security fix hidden in a public PR (see [SECURITY.md](SECURITY.md)).

## Output or interface change

<!--
If output changed, show a before/after snippet. Reviewers care most about the
offset model and JSON shape, since tooling downstream of --json depends on it.
-->

```diff

```

## Testing

<!-- How did you verify this? Commands, fixtures, sanitizer runs. -->
