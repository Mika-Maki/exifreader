# Architecture

**English** | [简体中文](../zh-CN/architecture.md)

This page explains how a file becomes an information tree, and which invariants
the code must not break. It is aimed at someone about to change the parser.

## Pipeline

```
file bytes
   |
   v
container sniff        src/container.cpp      -> ContainerInfo, EmbeddedExif[]
   |                                            (JPEG / PNG / TIFF / ISO-BMFF)
   v
TIFF header            src/tiff_parser.cpp    -> TiffHeaderInfo
   |                                            (byte order, magic, first IFD,
   |                                             classic vs BigTIFF)
   v
IFD graph walk         src/tiff_parser.cpp    -> IfdNode tree + TagEntry lists
   |                                            (IFD0, IFD1 chain, ExifIFD,
   |                                             GPS, Interop, SubIFDs,
   |                                             MakerNote blobs)
   v
value decoding         src/tiff_parser.cpp    -> formatted value + raw value
   |                     src/tiff_tags.cpp       (name lookup, enums, GPS, ...)
   v
rendering              src/renderer.cpp       -> tree / tags / JSON / summary
```

`main.cpp` owns the command line: it parses options, reads the input (whole file,
or streamed from a pipe), calls the parser, calls the renderer, and computes the
exit status from the result.

## Module map

| Module | Responsibility |
|--------|----------------|
| `exif_common.hpp` | shared types (`Endian`), byte-swap helpers, string utilities |
| `byte_reader.hpp` | `ByteReader`: a checked view over the file buffer; every accessor is bounds checked and reports failure via a `bool` return instead of throwing |
| `container.*` | format sniffing; produces `ContainerInfo` (format, detail, dimensions) and the list of `EmbeddedExif` payloads with their offsets |
| `tiff_parser.*` | `TiffHeaderInfo`, `TagEntry`, `IfdNode`, `ParseOptions`, `ParseResult`, and the `Parser` class that walks the IFD graph and decodes values |
| `tiff_tags.*` | the tag dictionaries: `TagInfo` tables for IFD0/IFD1/Exif/GPS/Interop and the vendor MakerNote dictionaries, plus the enum/formatter callbacks |
| `renderer.*` | the four renderers, output escaping, and `findThumbnails()` for `--extract` |

The parser never throws on malformed input and never allocates per tag beyond
the strings it returns. Failures are reported through `ParseResult::warnings`
and through missing optional fields, not exceptions.

## The two-coordinate offset model

Every directory and every value has two positions, and keeping them distinct is
the whole point of the tool:

* the **stored offset** - what the file actually wrote, relative to the TIFF
  header (or relative to the MakerNote blob's own header);
* the **absolute file offset** - where the bytes really are in the file.

Three helpers on `Parser` translate between the coordinate systems:

| Helper | Direction | Use |
|--------|-----------|-----|
| `readAt(stored)` | stored -> buffer index | where to read bytes from |
| `absOf(stored)` | stored -> file offset | what to print |
| `fileOf(bufferIndex)` | buffer index -> file offset | for values read out of line |

They are driven by three state variables:

```
tiffDelta_      buffer index of the TIFF header (top-level files)
blobDelta_      buffer index of a self-contained blob's TIFF header
blobFileBase_   file offset of the start of that blob
inBlob_         true while reading such a blob
```

At top level, `fileOf()` is the identity: the buffer *is* the file. Inside a blob
the mapping subtracts `blobDelta_` and adds `blobFileBase_`, so internal offsets
resolve while reported offsets stay in file coordinates.

### Self-contained MakerNote blobs

Some vendors embed a second, complete TIFF structure inside the MakerNote value
(Nikon type 3 and Panasonic). The parser copies that region into a self-contained
buffer, points `blobDelta_` at its header, and parses it with `inBlob_` set. The
save/restore around `parseEmbedded()` covers `tiffDelta_`, `blobDelta_`,
`blobFileBase_`, `inBlob_` and the BigTIFF/entry-width state, so a classic-entry
blob inside a BigTIFF file is parsed with its own 12-byte entry layout.

## Safety invariants

These are the rules that keep a hostile file from turning into a crash. A change
that weakens one of them is a security regression.

1. **No unchecked indexing.** Reads go through `ByteReader` (returns `false` on a
   short read) or through an explicit range check. Offsets that come straight from
   the file are validated before use.
2. **Saturating arithmetic.** `addClamped()` / `subClamped()` are used for offset
   arithmetic so that `offset + length` cannot wrap. Negative or absurd values
   (`SLONG`/`SRATIONAL` thumbnail offsets) are rejected, never wrapped.
3. **Entry counts are validated before use.** The count is compared against the
   space remaining in the file by *division*, not by multiplying the count by the
   entry size (which could wrap), and is additionally capped at
   `kMaxEntriesPerIfd` (2^20) per directory.
4. **Cycles are detected, not just survived.** Each visited directory offset is
   remembered; a repeat is reported as "already visited (cycle or shared
   pointer)" and not descended into. Real files legitimately share directories.
5. **Depth is capped.** `kMaxIfdDepth` is 256 and `--max-depth` is clamped to it
   in both the CLI and the library, because a deeper limit is only a stack
   overflow waiting for a hostile `SubIFDs` chain.
6. **Work is budgeted.** `--max-nodes` bounds the number of directories visited,
   so a file that is expensive but not infinite still terminates.
7. **Output cannot be forged.** `escapeForDisplay()` turns C0 controls and DEL
   into `\xNN` for the text renderers; `escapeJson()` guarantees valid UTF-8
   (invalid bytes become U+FFFD) and non-finite floats become the strings
   `"nan"`/`"inf"`/`"-inf"` so the JSON is always parseable.

`typeSize()` and the read path must agree on the width of every field type: the
IFD type (13) is 4 bytes wide in both, which is exactly the class of bug the
pre-release audit found.

## Value decoding

For each entry the parser decides whether the value is stored inline (it fits in
the 4-byte value field, 8 in BigTIFF) or out of line, reads it at the right
width and count, and then hands it to the tag table. The table entry decides the
human-readable form: a plain string, a number, a rational rendered as `1/125`,
an enum label, a GPS sexagesimal triple, a version string, and so on.

When a tag has no table entry it is still reported, with its id, width, count and
raw value - an unknown tag is data, not an error.

## Adding things

**A tag** - add an entry to the relevant table in `tiff_tags.cpp` (or a vendor
table for a MakerNote tag). Give it an id, a name, and a formatter if the raw
value needs interpretation. Then add an assertion to `tests/run_tests.sh` and,
if it needs a new byte layout, a fixture to `tests/make_fixtures.py`.

**A container** - teach `container.cpp` to recognise the signature and produce a
`ContainerInfo` plus the offsets of any EXIF payload. Everything downstream is
container-agnostic: it only needs to know where the TIFF header is. The test for
container support is that a fixture with a known layout parses to known offsets.

**An output mode** - add a renderer in `renderer.cpp` and wire the option in
`main.cpp`. Keep the JSON schema stable: scripts depend on the key names, and
the machine-readable modes are not localised.
