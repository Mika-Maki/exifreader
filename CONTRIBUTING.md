# Contributing to exifreader

**English** | [简体中文](CONTRIBUTING.zh-CN.md)

Thanks for taking the time to contribute. This document describes how to build
the project, what the code is expected to look like, and how to get a change
merged.

## Code of Conduct

This project follows the [Contributor Covenant](CODE_OF_CONDUCT.md). By
participating you are expected to uphold it. Report unacceptable behavior to
**conduct@example.com**.

## Ways to contribute

* **Bug reports** - use the [bug report form](.github/ISSUE_TEMPLATE/bug_report.yml).
  Malformed files are the interesting case: if a file crashes the reader, hangs
  it, or makes it print something it should not, that is a bug worth reporting.
* **Feature requests** - open a [feature request](.github/ISSUE_TEMPLATE/feature_request.yml)
  describing the metadata container or tag family you need.
* **Pull requests** - fixes, new tag dictionaries, new container/decode support,
  documentation and tests are all welcome.

Security issues must **not** be filed as public issues - see [SECURITY.md](SECURITY.md).

## Getting started

Requirements: a C++17 compiler (`g++` or `clang++`), `make`, and Python 3
(stdlib only) for the fixture generator. CMake ≥ 3.16 is optional.

```sh
git clone https://github.com/OWNER/exifreader.git
cd exifreader

make            # -> build/exifreader
make test       # build + the end-to-end suite (43 tests)
make debug      # ASan + UBSan build, then run the suite against it
```

The CMake path is equivalent:

```sh
cmake -S . -B build-cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-cmake -j
./tests/run_tests.sh build-cmake/exifreader
```

## Repository layout

| Path | Contents |
|------|----------|
| `src/` | the reader: container sniffing, TIFF parser, tag dictionaries, renderers, CLI |
| `tests/run_tests.sh` | end-to-end assertions over every output mode, including malformed input |
| `tests/make_fixtures.py` | generates the fixtures with known on-disk byte layouts |
| `docs/` | long-form documentation in English and Simplified Chinese, see [docs/i18n.md](docs/i18n.md) |
| `.github/workflows/` | CI, sanitizers and the release pipeline |
| `scripts/` | release and publishing helpers |

`tests/fixtures/` is generated, not committed - `make fixtures` recreates it.

If ASan cannot start on your kernel (some Android/Termux kernels map user space
below 40 bits), use `make ubsan` for a UBSan-only build.

## Coding style

* C++17, no third-party dependencies. The parser must stay allocation-light and
  must never throw on malformed input.
* 4-space indent, 100-column limit, braces attached to the statement. The
  style is defined by [`.clang-format`](.clang-format):

  ```sh
  make format        # reformat src/ in place
  make format-check  # report anything that is not clang-format clean
  ```

  The tree predates the config, so `make format-check` still reports
  pre-existing differences and CI runs it as an **advisory** job. Keep the files
  you touch clean; a drive-by reformat of unrelated code will be asked to split
  into its own pull request.

* Every read of file data goes through `ByteReader` (bounds checked) or an
  explicit range check. Never index a buffer with an offset that came straight
  from the file.
* Keep both the stored (TIFF-relative) and absolute file offset intact when you
  add a parse path - the offsets are the point of the tool.
* The build is warning-clean under `-Wall -Wextra -Wpedantic -Wshadow
  -Wconversion`; CI runs with `-Werror`. Do not silence a warning with a cast
  unless you can explain why the cast is provably in range.
* Comments explain *why*, not *what*. Match the surrounding tone: short,
  factual, no decoration.

## Tests

Any behavior change needs a fixture and an assertion. Add fixtures by extending
`tests/make_fixtures.py` (so the bytes stay reproducible in the repository) and
assertions to `tests/run_tests.sh`.

```sh
make test                                  # the whole suite
./tests/run_tests.sh build/exifreader      # against a specific binary
```

CI additionally runs the suite under ASan+UBSan and once per compiler, so a
change that only passes locally will be caught.

## Documentation and translations

English is the canonical documentation language and Simplified Chinese is
maintained alongside it. The rules - what is translated, what deliberately is
not, and how a new language is added - live in [docs/i18n.md](docs/i18n.md).

* Every translated page carries a language switcher on its first content line.
* When you change an English page, update its translation in the same pull
  request. If you cannot, say so in the pull request rather than letting it
  drift silently.
* Run the check before pushing:

  ```sh
  ./scripts/check-i18n.sh
  ```

  It fails when a translation is missing or a switcher does not point at its
  counterpart, and CI runs it as the `docs and i18n` job.

## Commit messages

Conventional Commits are used so the changelog can be generated:

```
<type>(<scope>): <subject>

<body>

<footer>
```

Common types: `feat`, `fix`, `perf`, `refactor`, `docs`, `test`,
`build`, `ci`, `chore`. Keep the subject in the imperative mood and under
72 characters. Reference issues with `Fixes #123` / `Closes #123`.

## Pull request process

1. Fork the repository and branch from `main`.
2. Make the change small and focused; avoid unrelated reformatting.
3. Run `make test` and `make format-check` locally.
4. Add or update the changelog entry under `## [Unreleased]` in
   [CHANGELOG.md](CHANGELOG.md) for user-visible changes.
5. Open the pull request and fill in the template. CI must be green.
6. A maintainer reviews; squash-merge is the default.

## Reporting performance or robustness problems

If the reader is slow, or a file makes it misbehave, please include:

* the exact command line and the exit code,
* the compiler and OS,
* a **minimised** input file if you can produce one (a truncated or re-hexed
  sample is ideal; strip anything private first - EXIF routinely contains GPS
  coordinates and device identifiers).

## License

By contributing you agree that your contributions are licensed under the
[MIT License](LICENSE).
