# FAQ

**English** | [简体中文](../zh-CN/faq.md)

## Is the output stable enough to parse?

The `--json` keys and the `--tags` four-field layout are treated as an interface.
They can change only in a release that says so in [CHANGELOG.md](../../CHANGELOG.md).
English prose in the tree mode is cosmetic and may be reworded.

## Why are offsets printed in two forms?

Because they answer different questions. `offset` (TIFF-relative) is what the
directory entry actually stored; `fileOffset` is where those bytes are in the
file. When you are comparing against a hex dump or repairing a pointer, you want
`fileOffset`. When you are checking that a value is consistent with what the
writer intended, the stored offset is the one that was written. See
[architecture.md](architecture.md#the-two-coordinate-offset-model).

## My file has EXIF but exifreader says nothing was found (exit 2)

Check what the container actually is. A PNG needs an `eXIf` chunk (many writers
put EXIF in a `tEXt` or XMP chunk instead). A JPEG needs an `APP1` segment whose
header is exactly `Exif\0\0`. A HEIC needs a usable `iinf`/`iloc`; the reader
falls back to scanning for a TIFF header, but a fragmented or unusual writer can
defeat both paths. Run with `--no-warnings` off (the default) and look at the
warning list.

## Why does it exit 3 when it clearly worked?

Because exit `3` means "read successfully, but the file has anomalies" - a
truncated entry, a directory cycle, a pointer past the end of the file. The
metadata is on stdout and is usually still useful. Use the exit code to decide
whether to trust it, not whether you got output.

## Why is MakerNote data still opaque?

Vendor MakerNotes are the least standardised part of EXIF: several vendors
encrypt them, encode them as an offset table, or change the layout between
camera generations. `exifreader` decodes the common plain-IFD and embedded-header
layouts and shows everything else with its size and a hex preview, rather than
guessing and printing wrong values. A wrong number is worse than an opaque one
in forensic work.

## Why is the tool's output not translated, while the docs are?

The output is an interface: tag names match the specifications you are debugging
against, and the JSON keys and field layout are parsed by scripts. Translating
them would break both. The documentation, which is read by a human, is translated
- see [i18n.md](../i18n.md).

## `--max-depth` refuses to go above 256. Why?

Because a deeper limit previously caused a stack overflow on a hostile `SubIFDs`
chain. Real files do not nest anywhere near 256 deep. The value is clamped in the
CLI *and* inside the parser, so a library user cannot raise it either.

## jq fails on the output when I pass several files

With more than one input, the JSON objects are concatenated (one per file), not
wrapped in an array - so the stream as a whole is not a single JSON document.
Process files one at a time, or use `--summary`. This is also why each object is
pretty-printed: it stays readable when concatenated.

## The extracted thumbnail will not open

`--extract` writes the bytes the file contains, verbatim. If the embedded
thumbnail is truncated or the `JPEGInterchangeFormat`/`Length` pair lies, the
written file inherits that. Verify the extracted bytes against the strip offset
and length reported in the tree.

## Where do the test fixtures come from?

`tests/make_fixtures.py` generates them, byte by byte, so every offset in the
expected output is known and reviewable. They are not committed - `make test`
regenerates them. That also means the suite proves the code matches *intended*
layouts, not that it matches your camera's files.

## Can I use it on a file I do not trust?

That is the case it is built for - see [SECURITY.md](../../SECURITY.md). The parser
is bounds checked, budgets its work, and escapes output. It is still
AI-generated code without an independent human security audit, so keep it in a
sandbox if the input is genuinely hostile.

## How do I cite or trust an AI-generated tool at all?

Read [AI_DISCLOSURE.md](../../AI_DISCLOSURE.md). The short version: verify the values
that matter against a second implementation, and treat the test suite as evidence
about intent rather than proof of correctness.
