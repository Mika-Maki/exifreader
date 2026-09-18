# Format and tag support

**English** | [简体中文](../zh-CN/formats.md)

What `exifreader` understands today, and - just as importantly - what it does
not. "Detected" means the container scan notices the payload; "decoded" means the
values are parsed.

## Containers

| Container | Detection | EXIF payload location | Notes |
|-----------|-----------|----------------------|-------|
| JPEG | signature `FF D8` | `APP1` segment with the `Exif\0\0` header | also reports dimensions from `SOFn` and the JFIF version |
| PNG | signature | `eXIf` chunk | |
| TIFF | `II*\0` / `MM\0*` / BigTIFF `43` | the file itself is the TIFF structure | classic and BigTIFF, both byte orders |
| HEIF / HEIC / AVIF | ISO-BMFF `ftyp` | the `Exif` item, located through `iinf` and `iloc` | falls back to scanning for a TIFF signature when the item table is unusable |

Not supported: WebP, JPEG 2000, JPEG XL, RAW formats that are not TIFF-based
(CR3 is ISO-BMFF and may partly work; the `Exif` item path is the same), and
sidecar files (`.xmp`, `.icc`).

## Field types

All TIFF field types are decoded, inline and out of line:

| Type | Id | Rendered as |
|------|----|-------------|
| BYTE | 1 | number, or hex with `--raw` |
| ASCII | 2 | string |
| SHORT | 3 | number |
| LONG | 4 | number |
| RATIONAL | 5 | `n/d` plus a decimal, and `1/125` for exposure times |
| SBYTE | 6 | signed number |
| UNDEFINED | 7 | hex preview, or a specific decode when the tag is known |
| SSHORT | 8 | signed number |
| SLONG | 9 | signed number |
| SRATIONAL | 10 | signed ratio |
| FLOAT | 11 | number (`"nan"`/`"inf"`/`"-inf"` in JSON) |
| DOUBLE | 12 | number, same rule |
| IFD | 13 | directory pointer, 4 bytes wide |
| LONG8 | 16 | BigTIFF 64-bit number |
| SLONG8 | 17 | BigTIFF 64-bit signed number |
| IFD8 | 18 | BigTIFF 64-bit directory pointer |

Byte order is taken from the TIFF header and applied to every multi-byte read,
including 64-bit BigTIFF fields.

## IFD graph

| Directory | Where it comes from |
|-----------|--------------------|
| IFD0 | the first IFD in the TIFF header |
| IFD1 | the `next IFD` chain from IFD0 (usually the thumbnail), up to 8 directories |
| ExifIFD | the `ExifIFDPointer` tag |
| GPS IFD | the `GPSIFDPointer` tag |
| Interop IFD | the `InteroperabilityIFDPointer` tag inside ExifIFD |
| SubIFDs | the `SubIFDs` tag, up to 64 entries |
| MakerNote | the `MakerNote` tag, parsed per vendor |

Every directory is reported with its stored and absolute offsets, its entry
count, its byte order and the next-IFD pointer.

## MakerNote vendors

The vendor is detected from the `Make` tag and the block's own signature, and a
dictionary is applied when one is known.

| Vendor | Layout handled | Dictionary |
|--------|----------------|------------|
| Nikon | type 3: embedded TIFF header, own byte order and magic | yes |
| Canon | plain IFD | yes |
| Apple | plain IFD | yes |
| Fujifilm | directory pointer with a vendor header | yes |
| Olympus | directory pointer | yes |
| Panasonic | embedded header | yes |
| Sony | plain IFD (several variants) | yes |
| Pentax, Samsung, Leica, Google, DJI | plain IFD | yes |

Unknown vendors, and unknown tags inside known vendors, are still shown: the
block is reported with its size and a hex preview rather than being dropped.
Deeply nested vendor structures that are encrypted or offset-table based (Canon
`CameraSettings`, Nikon `ShotInfo`) are shown as opaque data - they are not
decoded field by field.

## Semantic decoding

Beyond raw values, the decoder knows the meaning of:

* exposure time (as `1/125`), FNumber, aperture and APEX values,
* orientation, metering mode, flash, white balance, scene and exposure-program
  enumerations,
* GPS latitude/longitude/altitude, including the sexagesimal triple and the
  reference hemisphere,
* `UserComment` with its ASCII/UNICODE/JIS charset prefix,
* Windows `XP*` tags stored as UTF-16,
* EXIF version strings and `ComponentsConfiguration`,
* the `Copyright`, `Artist` and date/time families, with their format variants.

## Not decoded

Detected in the scan but not parsed:

* **XMP** (`APP1` with the `http://ns.adobe.com/xap/1.0/` header) - it is XML,
  a different parser.
* **IPTC/IIM** (`APP13` with the `Photoshop 3.0` header).
* **ICC profiles** (`APP2` with the `ICC_PROFILE` header).
* **JPEG comments** and other non-EXIF segments.

Also out of scope: writing or repairing metadata, re-encoding images, verifying
that the extracted thumbnail is a valid image, and any interpretation of the
image pixels themselves.
