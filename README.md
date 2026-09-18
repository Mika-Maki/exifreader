# exifreader - Image EXIF Information-Tree Reader

**English** | [简体中文](README.zh-CN.md)

[![CI](https://github.com/Mika-Maki/exifreader/actions/workflows/ci.yml/badge.svg)](https://github.com/Mika-Maki/exifreader/actions/workflows/ci.yml)
[![CodeQL](https://github.com/Mika-Maki/exifreader/actions/workflows/codeql.yml/badge.svg)](https://github.com/Mika-Maki/exifreader/actions/workflows/codeql.yml)
[![Release](https://github.com/Mika-Maki/exifreader/actions/workflows/release.yml/badge.svg)](https://github.com/Mika-Maki/exifreader/actions/workflows/release.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.cppreference.com/w/cpp/17)
[![Dependencies: none](https://img.shields.io/badge/dependencies-none-brightgreen.svg)](#build)
[![AI-generated](https://img.shields.io/badge/AI-generated-blueviolet.svg)](AI_DISCLOSURE.md)

A dependency-free C++17 command line tool that reads the EXIF/TIFF metadata of
an image and prints it as a navigable **information tree**: container -> EXIF
payload -> IFD directories -> tags -> decoded values, with offsets at every
level.

It answers more than "what is the ISO?" - it shows *where* every piece of
metadata lives (which segment, which IFD, which byte offset), which is what you
need when repairing or forensically inspecting a file.

> [!IMPORTANT]
> **This is an AI-generated project.** The source, tests, fixtures and
> documentation were produced by an AI coding agent under human direction, and
> have not been independently audited by a human expert. Please read
> [AI_DISCLOSURE.md](AI_DISCLOSURE.md) before relying on the output, and verify
> critical offsets and values against a second implementation.

    $ exifreader photo.jpg
    photo.jpg
     +- container: JPEG - JPEG (JFIF 1.2)  320x240  [baseline SOF0]
     |   +- EXIF payload: APP1 Exif segment at 0x2
     +- TIFF header @ 0xC: II (little-endian), magic 42, IFD0 at TIFF+0x8 (file 0x14)
      |-- IFD0  @ TIFF+0x8 (file 0x14)  12 entries, little-endian, next IFD at TIFF+0x9E
          Make (0x010F)  : ASCII[18] = NIKON CORPORATION
          Orientation (0x0112)  : SHORT[1] inline = 1  [normal]
          XResolution (0x011A)  : RATIONAL[1] = 300
          ExifIFDPointer (0x8769)  : LONG[1] inline = 260 (0x104)
        |-- ExifIFD  @ TIFF+0x104 (file 0x110)  23 entries, little-endian
            ExposureTime (0x829A)  : RATIONAL[1] = 0.008 (1/125)
            FNumber (0x829D)  : RATIONAL[1] = 2.8 (28/10)
            Flash (0x9209)  : SHORT[1] inline = 16  [did not fire, suppressed]
          |-- MakerNote (Nikon type 3)  @ TIFF+0xA4 (file 0xB0)  3 entries, little-endian
              ISO (0x0002)  : SHORT[1] inline = 400 (0x190)
              Quality (0x0004)  : ASCII[14] = FINE
        |-- GPSIFD  @ TIFF+0x21E (file 0x22A)  9 entries, little-endian
            GPSLatitude (0x0002)  : RATIONAL[3] = 48 (48/1), 51 (51/1), 2934 (2934/100)
      |-- IFD1  @ TIFF+0x9E (file 0xAA)  8 entries, little-endian
          JPEGInterchangeFormat (0x0201)  : LONG[1] inline = 1238 (0x4D6)
     = 5 IFD(s) (incl. 1 thumbnail), 54 tag(s)

## Features

* **Containers**: JPEG (APP1/Exif), PNG (`eXIf` chunk), raw TIFF (little and
  big endian, classic and BigTIFF), HEIF/HEIC/AVIF (ISO-BMFF `Exif` item located
  through `iinf`/`iloc`, with a signature-scan fallback).
* **Full IFD graph**: IFD0, the IFD1 thumbnail chain, ExifIFD, GPS IFD,
  Interop IFD, `SubIFDs`, and MakerNote blocks - each printed with its stored
  (TIFF-relative) *and* absolute file offset.
* **All field types** decoded: BYTE/SHORT/LONG/SLONG/RATIONAL/SRATIONAL/
  FLOAT/DOUBLE/ASCII/UNDEFINED, inline (<= 4 byte) and out-of-line values.
* **Semantic decoding**: exposure times as `1/125`, aperture, APEX values,
  orientation/metering/flash/white-balance/scene enums, GPS sexagesimal triples,
  `UserComment` charset prefixes (ASCII/UNICODE/JIS), Windows `XP*` UTF-16 tags,
  EXIF version strings, `ComponentsConfiguration`.
* **MakerNote awareness**: vendor detection plus dictionaries for Nikon, Canon,
  Apple, Fujifilm, Sony, Olympus, Panasonic, Pentax, Samsung, Leica, Google and
  DJI. Nikon type 3 (embedded TIFF header), Olympus/Fujifilm (directory pointer)
  and plain-IFD blocks are all handled; unrecognised blocks are still shown with
  size and a hex preview instead of being dropped.
* **Four output modes**: information tree (default), one tag per line, structured
  JSON that keeps both formatted values and offsets, and a one-line summary.
* **Thumbnail extraction** (`--extract DIR`).
* **Hostile-input hardening**: every read is bounds checked against the buffer,
  directories are cycle-checked, recursion depth and a directory budget are
  capped, and absurd entry counts / out-of-file payload pointers are reported as
  warnings instead of crashing.

## Build

Requirements: a C++17 compiler (g++ or clang++) plus make (or CMake >= 3.16).
No third-party libraries are used. `tests/make_fixtures.py` (stdlib-only
Python 3) generates the test images.

```sh
git clone https://github.com/Mika-Maki/exifreader.git
cd exifreader

make                 # -> build/exifreader
make test            # build + end-to-end tests (43 assertions)

sudo make install    # -> $(PREFIX)/bin/exifreader, PREFIX defaults to /usr/local
```

or with CMake:

```sh
cmake -S . -B build-cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-cmake --parallel
ctest --test-dir build-cmake --output-on-failure

cmake --install build-cmake --prefix /usr/local
```

Useful options and targets:

| Command | Effect |
|---------|--------|
| `make debug` | rebuild with ASan + UBSan |
| `make fixtures` | regenerate `tests/fixtures/` |
| `make format` / `make format-check` | clang-format `src/` (see CONTRIBUTING) |
| `make dist` | source tarball + checksum of `HEAD` in `dist/` |
| `cmake -DEXIFREADER_WERROR=ON` | treat compiler warnings as errors |

Prebuilt binaries for tagged releases are attached to the
[releases page](https://github.com/Mika-Maki/exifreader/releases) together with
`SHA256SUMS`.

## Usage

    exifreader [options] <image> [image ...]

    Output modes
      -t, --tree            indented information tree (default)
      --tags                one tag per line: ifd/path | name (id) | value | type
      -j, --json            structured JSON (raw + formatted values)
      -s, --summary         one compact line per image

    Content selection
      --no-values           omit values, print names/types only
      --raw                 also dump raw tag bytes (hex)
      --no-makers           do not parse MakerNote blocks
      --no-thumbnails       do not follow the IFD1 thumbnail directory
      --no-warnings         hide parser warnings
      --max-depth N         IFD recursion limit (default 8, clamped to 256)
      --max-nodes N         directory budget (default 200000)
      --extract DIR         write embedded thumbnails/previews into DIR

    Other
      --list-tags [KIND]    list a built-in dictionary (ifd0, ifd1, exif, gps,
                            interop, makernote or a vendor name)
      -h, --help            help
      -V, --version         version

A single `-` reads the image from stdin.

### Exit status

| code | meaning |
|------|---------|
| 0    | EXIF read successfully, no anomalies |
| 1    | usage error, or the file could not be read |
| 2    | the file was read but no EXIF/TIFF data was found |
| 3    | EXIF found, but the file reports anomalies (warnings) |

The exit status makes the tool usable as a filter: `3` means the metadata is
suspect, not that the run failed.

### Examples

    exifreader photo.jpg                       # the information tree
    exifreader --tags --no-values *.jpg        # an index of every tag
    exifreader --json photo.heic | jq .exif.ifds[0].tags
    exifreader --extract out --summary gallery/*.jpg
    exifreader --list-tags exif                # what the reader knows
    cat photo.jpg | exifreader -               # straight from a pipe

## Layout

    src/exif_common.hpp       shared types and endian helpers
    src/byte_reader.hpp       bounds-checked view over the file buffer
    src/container.hpp/.cpp    container sniffing (JPEG/PNG/TIFF/HEIF)
    src/tiff_parser.hpp/.cpp  the IFD tree parser and value decoding
    src/tiff_tags.hpp/.cpp    tag dictionaries (EXIF/GPS/Interop/MakerNotes)
    src/renderer.hpp/.cpp     tree / tags / JSON / summary renderers
    src/main.cpp              command line interface
    tests/make_fixtures.py    generates images with known byte layouts
    tests/run_tests.sh        end-to-end test suite
    scripts/                  release and publishing helpers
    .github/                  CI, CodeQL, release workflow and templates

### Offset model

Each directory and tag value tracks two positions: the **stored** offset as
written in the file (relative to the TIFF header) and the absolute **file**
offset. `readAt()` maps stored -> buffer index, `absOf()` maps stored -> file
offset. Vendor MakerNote blocks that carry their own TIFF header (Nikon type 3,
Panasonic) are parsed from a self-contained copy with `blobDelta_` set, so their
internal offsets resolve while reported offsets stay in file coordinates.

## Testing

`tests/run_tests.sh` builds no code: it generates the fixtures and asserts the
observable behaviour of every mode, including malformed inputs.

    $ make test
    exifreader test suite
    ...
    passed: 43  failed: 0

Fixtures cover a JPEG with the full IFD graph (Exif/GPS/Interop/SubIFD/thumbnail),
a JPEG without the Make tag, one without GPS/MakerNote, PNG, raw TIFF,
big-endian TIFF, HEIF, two MakerNote layouts (Nikon type 3 and a plain-IFD
vendor block), a JPEG with no metadata, plus truncated, cyclic, absurd-count,
out-of-file-pointer, empty and non-image files.

CI runs the suite with g++ and clang++, through both the Make and CMake
builds, and again under ASan + UBSan.

ASan needs a kernel with a large enough user virtual address space. On kernels
that map user space below 40 bits - some Android/Termux kernels - it aborts at
startup with `heap size ... exceeds max user virtual address`. Use the UBSan-only
build there:

```sh
make ubsan && ./tests/run_tests.sh build/exifreader     # UBSan, no ASan
make debug && ./tests/run_tests.sh build/exifreader     # ASan + UBSan
```

The suite is clean under both over the whole fixture set, in every mode.

## Robustness notes

* `--max-depth` is clamped to 256: a deeper limit only ever bought a stack
  overflow on hostile SubIFD chains, so values above it are reduced (with a
  note on stderr).
* BigTIFF uses byte-order-aware 64-bit reads; classic entries embedded in a
  BigTIFF file (for example a Nikon type 3 MakerNote) keep their own 2-byte
  counts and 12-byte entries, and IFD pointers honour the width of their tag
  type (LONG stays 4 bytes even in BigTIFF).
* Tag values are sanitised on output: C0 controls and DEL are printed as
  `\xNN` in the tree/tags/summary modes, so a hostile payload cannot inject
  newlines or ANSI escapes. JSON output is always valid UTF-8: malformed
  bytes become U+FFFD.
* Non-finite FLOAT/DOUBLE values (NaN/Inf) are emitted as the JSON strings
  `"nan"`, `"inf"` and `"-inf"`; a bare `nan`/`inf` token is not legal JSON.
* Inputs whose length cannot be determined (procfs files, FIFOs) are read as
  a stream instead of being treated as empty.
* Unsigned offset arithmetic is saturating and out-of-range offsets/lengths
  (including negative SLONG/SRATIONAL thumbnail values) are rejected instead
  of being wrapped into the buffer.

The pre-release hardening pass that produced these rules is summarised in
[CHANGELOG.md](CHANGELOG.md); the security requirements it implies are in
[SECURITY.md](SECURITY.md).

## Limitations

* MakerNote dictionaries are intentionally small: unknown tags are printed with
  their id and value but no name, and deeply nested vendor sub-structures
  (encrypted or offset-table based, such as Canon `CameraSettings` or Nikon
  `ShotInfo`) are shown as opaque data rather than decoded field by field.
* The `SubIFDs` chain is limited to 64 entries per tag and the IFD1 sibling
  chain to 8 directories.
* Only EXIF/TIFF metadata is read: XMP, IPTC and ICC payloads are detected in
  the container scan but not decoded.
* Thumbnail extraction writes the embedded JPEG/strip data verbatim; it does not
  re-encode or verify the extracted image.

## Documentation

* [docs/](docs/README.md) - the documentation index.
* [Usage guide](docs/en/usage.md) - every mode, option, exit code and recipe.
* [Architecture](docs/en/architecture.md) - pipeline, offset model, safety invariants.
* [Format support](docs/en/formats.md) - containers, field types, MakerNote vendors.
* [FAQ](docs/en/faq.md) - the questions people actually ask.
* [i18n policy](docs/i18n.md) - languages, what is translated and what is not.

中文文档:[README](README.zh-CN.md)、[使用指南](docs/zh-CN/usage.md)、
[架构说明](docs/zh-CN/architecture.md)、[格式支持](docs/zh-CN/formats.md)、
[常见问题](docs/zh-CN/faq.md)。

## Contributing

Contributions are welcome. Read [CONTRIBUTING.md](CONTRIBUTING.md) for the build
commands, the coding style and the commit conventions, and
[CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md) for the community expectations. Bug
reports and feature requests have GitHub issue forms.

Because this project is AI-generated, contributions may be AI-generated too -
please say so in the pull request, and take responsibility for the change either
way.

## Security

exifreader parses untrusted files. Please report vulnerabilities privately
through [SECURITY.md](SECURITY.md), never in a public issue.

## AI disclosure

This project is AI-generated. See [AI_DISCLOSURE.md](AI_DISCLOSURE.md) for what
that covers, what it means for the correctness of the offsets and decoded
values, and the transparency conventions used in this repository.

## License

[MIT](LICENSE) (c) 2026 exifreader contributors.
