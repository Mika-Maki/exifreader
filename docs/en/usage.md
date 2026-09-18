# Usage guide

**English** | [简体中文](../zh-CN/usage.md)

`exifreader` reads the EXIF/TIFF metadata of one or more files and renders it.
This page is the reference for the command line; the [README](../../README.md) has
the short version.

## Synopsis

```
exifreader [options] <image> [image ...]
```

A single `-` in place of a file name reads the image from standard input. If no
file is given, usage is printed and the exit status is `1`.

## Output modes

Exactly one mode applies; the last one on the command line wins. The default is
the information tree.

### Information tree (`-t`, `--tree`)

The default. Nested directories are drawn with indentation, and every directory
and value carries its stored offset and its absolute file offset.

```sh
exifreader photo.jpg
```

### One tag per line (`--tags`)

A flat index, one tag per line, pipe-separated:

```
IFD0 | Make (0x010F) | NIKON CORPORATION | ASCII[18]
```

The four fields are `path`, `name (id)`, `value`, `type[count]`. This mode is
the easiest to post-process with `awk`, `cut` or `grep`.

```sh
exifreader --tags photo.jpg | cut -d'|' -f2 | sort -u
```

### JSON (`-j`, `--json`)

Structured output with both the formatted and the raw representation of every
value, plus offsets. The shape is:

```json
{
  "source": "photo.jpg",
  "container": {
    "format": "JPEG",
    "detail": "JPEG",
    "width": 16,
    "height": 16,
    "exifPayloads": [{"offset": "0xC", "note": "APP1 Exif segment at 0x2"}]
  },
  "exif": {
    "found": true,
    "tiffOffset": "0xC",
    "endianness": "little",
    "bigTiff": false,
    "ifdCount": 5,
    "tagCount": 54,
    "makerNoteVendor": "Nikon",
    "warnings": [],
    "ifds": [
      {
        "ifd": "IFD0",
        "kind": "IFD0",
        "offset": "TIFF+0x8",
        "fileOffset": "0x14",
        "endianness": "little",
        "maker": "NIKON CORPORATION",
        "entryCount": 12,
        "nextIfd": "TIFF+0x9E",
        "tags": [
          {
            "id": "0x010F",
            "name": "Make",
            "type": "ASCII",
            "count": 18,
            "offset": "0x36E",
            "fileOffset": "0x37A",
            "inline": false,
            "value": "NIKON CORPORATION"
          }
        ],
        "children": []
      }
    ]
  }
}
```

Notes worth knowing before you parse it:

* **Nested directories live in `children`.** ExifIFD, GPS, Interop, SubIFDs and
  MakerNote blocks are child objects of the IFD that points at them. The top-level
  `ifds` array holds IFD0 and the IFD1 thumbnail chain.
* **Offsets are strings**, in `0x` hexadecimal. `offset` is TIFF-relative and
  `fileOffset` is absolute within the file.
* **With several inputs the objects are concatenated**, not wrapped in an array -
  one pretty-printed object per file. Use `jq` per object, or `-s` if you only
  need a summary.
* **Non-finite floats are strings.** `"nan"`, `"inf"` and `"-inf"` are emitted
  as JSON strings, because bare `nan`/`inf` are not valid JSON. Numeric-looking
  values may therefore be either a number or one of those strings.
* **Output is always valid UTF-8.** Bytes that are not valid UTF-8 in a tag value
  become U+FFFD.
* **A failed parse has a different shape.** When no EXIF/TIFF data can be parsed
  (exit status `2`), `exif` is `{"found": false, "error": "no EXIF data"}` and the
  IFD keys are absent. Check `exif.found` before indexing into `ifds`.

```sh
exifreader --json photo.jpg | jq '.exif.ifds[0].tags[] | select(.name == "Make")'
exifreader --json photo.jpg | jq -r '.exif.warnings[]'
```

### Summary (`-s`, `--summary`)

One line per image: container and dimensions, byte order, IFD and tag counts,
then the handful of tags a person usually wants.

```sh
exifreader --summary gallery/*.jpg
```

```
JPEG 16x16, little-endian, 5 IFD(s), 54 tag(s), Make=NIKON CORPORATION, Model=NIKON Z 8, \
DateTimeOriginal=2024:05:17 18:23:45, ExposureTime=0.008 (1/125), FNumber=2.8 (28/10), \
ISO=200, MakerNote=Nikon
```

## Content selection

| Option | Effect |
|--------|--------|
| `--no-values` | print names, ids and types but no values |
| `--raw` | also dump the raw bytes of each value as hex |
| `--no-makers` | do not descend into MakerNote blocks |
| `--no-thumbnails` | do not follow the IFD1 thumbnail directory |
| `--no-warnings` | hide parser warnings from the tree and the summary |

`--no-values` plus `--tags` is the fastest way to inventory what a file carries:

```sh
exifreader --tags --no-values *.jpg | sort -u
```

## Resource limits

| Option | Default | Meaning |
|--------|---------|---------|
| `--max-depth N` | 8 | maximum IFD nesting depth; **clamped to 256** |
| `--max-nodes N` | 200000 | budget of directories visited before parsing stops |

Both exist so that a hostile file cannot make the reader recurse until the stack
overflows or allocate without bound. `--max-depth` above 256 is reduced and a
note is printed on stderr - a deeper limit only ever bought a stack overflow.

`--max-nodes` counts directories, not tags. When the budget is exhausted the
parse stops and a warning is recorded; the tag counts in the output then reflect
what was actually visited.

## Extracting thumbnails

```sh
exifreader --extract out gallery/*.jpg
```

Each embedded thumbnail or preview is written verbatim into `out/`, named after
the source file with an index suffix, and the reader prints where it wrote:

```
extracted thumbnail -> out/photo.jpg.thumb01.jpg (155 bytes)
```

The bytes are not re-encoded or validated - the tool writes what the file
claims. Treat extracted output as untrusted if the input was untrusted.

## Listing the built-in dictionaries

```sh
exifreader --list-tags                 # the default (IFD0) dictionary
exifreader --list-tags exif            # ExifIFD
exifreader --list-tags gps             # GPS
exifreader --list-tags nikon           # a vendor MakerNote dictionary
```

Known kinds are `ifd0`, `ifd1`, `exif`, `gps`, `interop`, `makernote` and the
vendor names (Nikon, Canon, Apple, Fujifilm, Sony, Olympus, Panasonic, Pentax,
Samsung, Leica, Google, DJI). Each listing shows the tag id, the name, and the
value meanings the decoder knows.

## Exit status

| Code | Meaning | Typical cause |
|------|---------|---------------|
| 0 | read successfully, no anomalies | |
| 1 | usage error, or the file could not be read | bad option, missing file, permission denied |
| 2 | the file was read, but no EXIF/TIFF data could be parsed (output: `no EXIF data`) | PNG without `eXIf`, JPEG without APP1, non-image file, a JPEG truncated before its EXIF payload |
| 3 | EXIF was parsed, but the file reports anomalies (warnings) | truncated entry, directory cycle, out-of-file pointer |

Exit code `3` is a *success with caveats*, not a failure: the useful metadata is
still on stdout. A script that wants "strictly clean" should test for `0`
explicitly.

```sh
if exifreader --summary photo.jpg >/dev/null; then
    echo "clean read"
elif [ $? -eq 3 ]; then
    echo "read, but the file has anomalies"
fi
```

## Reading from a pipe

```sh
cat photo.jpg | exifreader -
curl -s https://example.invalid/photo.jpg | exifreader --summary -
```

Inputs whose length cannot be determined (FIFOs, procfs entries) are read as a
stream instead of being treated as an empty file, so `-` works on pipes.

## Recipes

Find the capture time of every image in a directory:

```sh
exifreader --summary *.jpg | grep -o 'DateTimeOriginal=[^,]*'
```

List images taken at a given location, by GPS:

```sh
for f in *.jpg; do
    lat=$(exifreader --json "$f" | jq -r '.exif.ifds[] | .children[]? | select(.kind=="GPS") | .tags[] | select(.name=="GPSLatitude") | .value')
    [ -n "$lat" ] && echo "$f $lat"
done
```

Check whether a file's metadata is self-consistent:

```sh
exifreader --json photo.jpg | jq '{warnings: .exif.warnings, ifds: .exif.ifdCount}'
```

## Troubleshooting

**"no EXIF data" (exit 2)** - the file has no EXIF/TIFF payload. A PNG
needs an `eXIf` chunk, a JPEG an `APP1 Exif` segment. HEIC support depends on
locating the `Exif` item in the ISO-BMFF metadata.

**"IFD at 0x... already visited"** - the file points a directory at an offset it
already used. Real files do this for shared directories; hostile files do it to
create cycles. The reader stops descending and records a warning.

**Offsets look wrong** - remember there are two: `offset` is relative to the
TIFF header, `fileOffset` is the real position in the file. When comparing
against a hex dump, use `fileOffset`.

**The reader stops early** - you probably hit `--max-nodes`. Raise it
explicitly if you trust the file.
