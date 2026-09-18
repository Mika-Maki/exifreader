# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

> **Language:** this file is maintained in English only - see
> [docs/i18n.md](docs/i18n.md) for why. Chinese documentation:
> [README.zh-CN.md](README.zh-CN.md) ·
> [docs/zh-CN/](docs/zh-CN/README.md).

## [Unreleased]

Nothing yet.

## [1.0.2] - 2026-09-18

### Added

* `fuzz/fuzz_exif.cc`, a libFuzzer harness covering the container sniffer, the
  TIFF parser, all four renderers and thumbnail discovery, plus `make fuzz`
  (needs clang), the `-DEXIFREADER_FUZZ=ON` CMake option and a CI job that runs
  a short campaign on every push. The fuzzing claim in the security policy is
  now reproducible from the repository.
* Regression assertions for the exit status 2 message and for the `--max-nodes`
  validation.

### Changed

* `--max-nodes` validates the way `--max-depth` does: a non-numeric value is
  rejected with exit status 1 instead of being read as 0, an out-of-range value
  is clamped with a note on stderr, and a negative literal no longer wraps
  through `strtoull` into a huge budget.
* Every output mode now reports a failed parse with the same words
  (`no EXIF data`), and the JSON failure shape carries that reason in
  `exif.error` instead of an empty string.
* Documentation no longer hardcodes the size of the test suite, and the exit
  status 2 rows agree between the README and the usage guide.

### Fixed

* `scripts/release.sh` deleted the body of the `[Unreleased]` section when it
  inserted a new version, because it spliced from `## [Unreleased]` to the first
  released heading. It now inserts the section in place and also refreshes the
  `[Unreleased]` compare link and the new version's release link.

### Removed

* Dead configuration and helpers that were never wired to anything:
  `RenderOptions::color`, `RenderOptions::showTypes`,
  `ContainerInfo::extraPayloads`, `EmbeddedExif::payloadSize`,
  `exif_common.hpp::halfToDouble` and `tiff_tags.hpp::makerNoteKind`.

## [1.0.1] - 2026-09-18

### Fixed

* **Use-after-free in the tag index cache.** `indexFor()` returned a reference
  to an element of a `std::vector` while releasing the mutex on the way out, so
  the next table that grew the cache reallocated the vector and left the caller
  holding a dangling reference. The cache is a `std::deque` now, whose
  `push_back` never invalidates references to existing elements.
* **Dead contact addresses in the published repository.** `SECURITY.md` and its
  Chinese edition pointed reporters at `security@example.com`, and
  `CODE_OF_CONDUCT.md` and both `CONTRIBUTING` files at
  `conduct@example.com`. GitHub private vulnerability reporting was disabled as
  well, so the advisory link in the security policy was dead too: a reporter had
  no working channel and would have waited for a reply that could not arrive. The
  security policy now uses private vulnerability reporting only (enabled on the
  repository), states plainly that there is no security email, and offers a
  no-details fallback for people who cannot use the form; conduct reports go to
  GitHub's report abuse form, or to the private reporting form when the matter
  is project-specific and must stay confidential.
* `scripts/publish.sh --no-rewrite` skipped the unresolved-placeholder check
  along with the rewriting, so a repository whose security policy pointed at a
  placeholder address could still be published. The gate now runs on every real
  publish regardless of `--no-rewrite`, and covers placeholder domains
  (`example.com`/`.org`/`.net`, `your-email`, `changeme`, `REPLACE_ME`) as well
  as `github.com/OWNER`, printing the offending file and line. The same rule is
  enforced in CI for the contact-bearing documents.
* The advisory `clang-format` job called `make format-check` directly, which put
  a red cross on every commit even though the workflow succeeded; it reports the
  differences as a warning annotation now.

## [1.0.0] - 2026-09-18

The first public release.

### Added

* **Containers**: JPEG (`APP1/Exif`), PNG (`eXIf` chunk), raw TIFF (classic and
  BigTIFF, both byte orders), and HEIF/HEIC/AVIF via ISO-BMFF `iinf`/`iloc`
  with a signature-scan fallback.
* **Full IFD graph**: IFD0, the IFD1 thumbnail chain, ExifIFD, GPS IFD, Interop
  IFD, `SubIFDs` and MakerNote blocks, each reported with both its stored
  (TIFF-relative) and absolute file offset.
* **Value decoding** for every TIFF field type - BYTE/SHORT/LONG/SLONG/RATIONAL/
  SRATIONAL/FLOAT/DOUBLE/ASCII/UNDEFINED, plus the BigTIFF LONG8/SLONG8/IFD8
  types - inline and out-of-line.
* **Semantic decoding**: exposure times as `1/125`, aperture, APEX values,
  orientation/metering/flash/white-balance/scene enumerations, GPS sexagesimal
  triples, `UserComment` charset prefixes, Windows `XP*` UTF-16 tags, EXIF
  version strings and `ComponentsConfiguration`.
* **MakerNote awareness** for Nikon, Canon, Apple, Fujifilm, Sony, Olympus,
  Panasonic, Pentax, Samsung, Leica, Google and DJI, including Nikon type 3
  (embedded TIFF header) and Olympus/Fujifilm directory-pointer layouts.
* **Four output modes**: information tree (default), one tag per line,
  structured JSON with raw and formatted values, and a one-line summary.
* **CLI**: content selection (`--no-values`, `--raw`, `--no-makers`,
  `--no-thumbnails`, `--no-warnings`), resource limits (`--max-depth`,
  `--max-nodes`), `--extract DIR` for embedded thumbnails, `--list-tags` for
  the built-in dictionaries, and stdin input via `-`.
* **Hostile-input hardening** across the whole parse path, with bounds checks,
  saturating offset arithmetic, directory cycle detection, recursion and
  directory budgets, and output escaping.
* End-to-end test suite (`tests/run_tests.sh`) over every output
  mode, driven by generated fixtures (`tests/make_fixtures.py`) with known
  on-disk byte layouts for JPEG, PNG, TIFF, big-endian TIFF, BigTIFF, HEIF and
  two MakerNote layouts.
* **Documentation set** in `docs/`: a usage guide, an architecture guide with
  the offset model and the safety invariants, a format/tag support matrix and a
  FAQ - each in English and Simplified Chinese.
* **Internationalisation**: Simplified Chinese editions of the README,
  CONTRIBUTING, SECURITY and AI disclosure; a language switcher on every
  translated page; `docs/i18n.md` defining what is translated and what is not;
  and `scripts/check-i18n.sh`, enforced by the CI `docs and i18n` job, which
  fails the build on a missing translation or a broken switcher.
* **AI disclosure**: `AI_DISCLOSURE.md` (and its Chinese edition) plus an
  AI-generated badge and notice in the README.
* Make and CMake builds; `make debug` for an ASan+UBSan build.
* GitHub Actions CI (gcc/clang, CMake, sanitizers, formatting) and a tagged
  release workflow that attaches checksummed binaries to a GitHub Release.

### Fixed

Defects found by the pre-release hardening audit and fixed before this
release:

* Out-of-bounds read when a tag used the IFD field type (13): the type width is
  4 bytes, and `typeSize()` now agrees with the read path.
* BigTIFF parsing: the 64-bit first-IFD pointer, directory count, next-IFD
  pointer and out-of-line payload pointers are read byte-order-correctly, the
  value field is located at `entry + 4 + offsetBytes`, and LONG-valued IFD
  pointers keep their 4-byte width inside a BigTIFF file.
* A 28-byte BigTIFF file could hang the parser: entry counts are now compared by
  division against the remaining file size (no multiplicative wraparound) and
  capped per directory.
* Undefined behaviour when converting non-finite or out-of-range FLOAT/DOUBLE
  values; non-finite values are also valid JSON now.
* Negative or overflowing thumbnail offsets were wrapped into the buffer;
  `toSize()` rejects non-finite, negative and ≥ 2^64 values.
* `--max-depth` could be raised far enough to overflow the stack on a hostile
  `SubIFDs` chain; it is clamped to 256.
* Tag values could inject newlines and terminal escapes into the output; control
  characters are escaped and JSON output is always valid UTF-8.
* Absolute file offsets were double-counted (the TIFF base was added twice), so
  every reported file offset is now the true offset in the file.
* Classic MakerNotes embedded in a BigTIFF file were parsed with BigTIFF entry
  widths instead of their own header's format.

[Unreleased]: https://github.com/Mika-Maki/exifreader/compare/v1.0.1...HEAD
[1.0.1]: https://github.com/Mika-Maki/exifreader/releases/tag/v1.0.1
[1.0.0]: https://github.com/Mika-Maki/exifreader/releases/tag/v1.0.0
