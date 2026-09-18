#!/usr/bin/env python3
"""Generate EXIF test fixtures for exifreader (no external libraries).

Builds a JPEG, a PNG, a raw TIFF, a HEIF/ISO-BMFF sample and a set of corrupt
files. Every fixture is written by hand so the byte layout is fully known.
"""
import os
import struct
import sys

OUT = sys.argv[1] if len(sys.argv) > 1 else "fixtures"
os.makedirs(OUT, exist_ok=True)

BYTE, ASCII, SHORT, LONG, RATIONAL, UNDEFINED, SRATIONAL = 1, 2, 3, 4, 5, 7, 10
SIZES = {BYTE: 1, ASCII: 1, SHORT: 2, LONG: 4, RATIONAL: 8, UNDEFINED: 1, SRATIONAL: 8}


def s(value):
    return value.encode("ascii") + b"\x00"


def rat(n, d=1):
    return (n, d)


# A 1x1 JPEG used as embedded thumbnail data.
THUMB = bytes.fromhex(
    "ffd8ffe000104a46494600010100000100010000ffdb004300ffffffffffffffffffffffff"
    "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
    "ffc2000b080001000101011100ffc40014000100000000000000000000000000000009ffda"
    "00080100000000013fffc40014100100000000000000000000000000000000ffda00080101"
    "000001063fffd9"
)


class Ifd:
    def __init__(self):
        self.entries = []   # (tag, type, count, payload_bytes)

    def add(self, tag, typ, values, raw=None):
        if raw is not None:
            payload = raw
            count = len(raw)
        elif typ == ASCII:
            payload = values if isinstance(values, bytes) else s(values)
            count = len(payload)
        else:
            if not isinstance(values, (list, tuple)):
                values = [values]
            if typ == SHORT:
                payload = b"".join(struct.pack("<H", v) for v in values)
            elif typ == LONG:
                payload = b"".join(struct.pack("<I", v) for v in values)
            elif typ in (RATIONAL, SRATIONAL):
                payload = b"".join(struct.pack("<II", v[0] & 0xFFFFFFFF, v[1] & 0xFFFFFFFF)
                                   for v in values)
            elif typ in (BYTE, UNDEFINED):
                payload = bytes(values)
            else:
                raise ValueError("type %d" % typ)
            count = len(values)
        self.entries.append((tag, typ, count, payload))
        return self


def ifd_parts(entries, next_ifd=0):
    """Serialize one IFD into (directory_bytes, out_of_line_payloads).

    The caller decides where the payloads land and patches the offsets with
    ifd_patch_offsets(), which keeps every directory at a predictable spot."""
    entries = sorted(entries, key=lambda e: e[0])
    out = struct.pack("<H", len(entries))
    extra = b""
    for tag, typ, count, payload in entries:
        size = SIZES.get(typ, 1) * count
        if size <= 4:
            field = payload.ljust(4, b"\x00")
        else:
            if len(payload) != size:
                raise ValueError("payload/count mismatch for tag 0x%04x" % tag)
            field = struct.pack("<I", 0xDEADBEEF)   # patched afterwards
            extra += payload
            if len(extra) % 2:
                extra += b"\x00"
        out += struct.pack("<HHI", tag, typ, count) + field
    out += struct.pack("<I", next_ifd)
    return out, extra


def ifd_patch_offsets(dir_bytes, entries, payload_abs):
    """Fill in the out-of-line pointers, assuming the packed payload area
    starts at payload_abs (relative to the TIFF header)."""
    entries = sorted(entries, key=lambda e: e[0])
    out = bytearray(dir_bytes)
    running = payload_abs
    for i, (tag, typ, count, payload) in enumerate(entries):
        size = SIZES.get(typ, 1) * count
        if size <= 4:
            continue
        struct.pack_into("<I", out, 2 + 12 * i + 8, running)
        running += len(payload)
        if running % 2:
            running += 1
    return bytes(out)


def build_tiff(maker="NIKON CORPORATION", model="NIKON Z 8", with_gps=True,
               with_thumbnail=True, with_makernote=True, with_subifd=False):
    """Assemble a little-endian TIFF/EXIF block.

    The layout is solved iteratively: directory offsets decide the pointer
    values, and pointer values never change a directory's size, so the loop
    converges in two or three passes. Returns (bytes, ifd0_abs).
    """
    header = b"II*" + bytes([0]) + struct.pack("<I", 8)

    exif = Ifd()
    exif.add(0x829A, RATIONAL, [rat(1, 125)])
    exif.add(0x829D, RATIONAL, [rat(28, 10)])
    exif.add(0x8822, SHORT, [3])
    exif.add(0x8827, SHORT, [200])
    exif.add(0x9000, UNDEFINED, None, raw=b"0232")
    exif.add(0x9003, ASCII, "2024:05:17 18:23:45")
    exif.add(0x9004, ASCII, "2024:05:17 18:23:45")
    exif.add(0x9201, RATIONAL, [rat(6965784, 1000000)])
    exif.add(0x9202, RATIONAL, [rat(2970854, 1000000)])
    exif.add(0x9204, SRATIONAL, [rat(4294967295, 3)])   # -1/3 in two's complement
    exif.add(0x9207, SHORT, [5])
    exif.add(0x9209, SHORT, [16])
    exif.add(0x920A, RATIONAL, [rat(700, 10)])
    exif.add(0x9286, UNDEFINED, None, raw=b"ASCII" + bytes([0, 0, 0]) + b"Holiday snapshot, handheld.")
    exif.add(0xA001, SHORT, [1])
    exif.add(0xA002, LONG, [8256])
    exif.add(0xA003, LONG, [5504])
    exif.add(0xA005, LONG, [0])            # patched -> InteropIFD
    exif.add(0xA402, SHORT, [0])
    exif.add(0xA403, SHORT, [0])
    exif.add(0xA405, SHORT, [105])
    exif.add(0xA434, ASCII, "NIKKOR Z 24-70mm f/2.8 S")
    if with_makernote:
        exif.add(0x927C, UNDEFINED, None, raw=b"Nikon" + bytes([0, 2, 0x10, 0]) + b"MN" * 6)

    gps = Ifd()
    gps.add(0x0000, BYTE, [2, 3, 0, 0])
    gps.add(0x0001, ASCII, "N")
    gps.add(0x0002, RATIONAL, [rat(48, 1), rat(51, 1), rat(2934, 100)])
    gps.add(0x0003, ASCII, "E")
    gps.add(0x0004, RATIONAL, [rat(2, 1), rat(21, 1), rat(372, 100)])
    gps.add(0x0005, BYTE, [0])
    gps.add(0x0006, RATIONAL, [rat(35, 1)])
    gps.add(0x0007, RATIONAL, [rat(17, 1), rat(23, 1), rat(45, 1)])
    gps.add(0x001D, ASCII, "2024:05:17")

    interop = Ifd()
    interop.add(0x0001, ASCII, "R98")
    interop.add(0x0002, UNDEFINED, None, raw=b"0100")

    subifd = Ifd()
    subifd.add(0x0100, LONG, [2048])
    subifd.add(0x0101, LONG, [1024])
    subifd.add(0x0103, SHORT, [1])
    subifd.add(0x0112, SHORT, [1])
    subifd.add(0x0132, ASCII, "2024:05:17 18:23:45")

    makernote = Ifd()
    makernote.add(0x0001, UNDEFINED, None, raw=b"0300")
    makernote.add(0x0002, SHORT, [200])
    makernote.add(0x0004, ASCII, "FINE  ")
    makernote.add(0x0005, SHORT, [0])
    makernote.add(0x0083, BYTE, [1])
    makernote.add(0x0084, RATIONAL, [rat(240, 10), rat(700, 10), rat(28, 10), rat(28, 10)])
    makernote.add(0x0087, BYTE, [0])
    makernote.add(0x0095, ASCII, "OFF")
    makernote.add(0x00A7, LONG, [10473])
    makernote.add(0x00B6, UNDEFINED, None, raw=bytes([7, 0xE8, 5, 0x11, 0x12, 0x17, 0x2D, 0]))

    ifd0 = Ifd()
    ifd0.add(0x010F, ASCII, maker)
    ifd0.add(0x0110, ASCII, model)
    ifd0.add(0x0112, SHORT, [1])
    ifd0.add(0x011A, RATIONAL, [rat(300, 1)])
    ifd0.add(0x011B, RATIONAL, [rat(300, 1)])
    ifd0.add(0x0128, SHORT, [2])
    ifd0.add(0x0131, ASCII, "exifreader test suite")
    ifd0.add(0x0132, ASCII, "2024:05:17 18:23:45")
    ifd0.add(0x013B, ASCII, "Test Author")
    ifd0.add(0x8298, ASCII, "CC0")
    ifd0.add(0x8769, LONG, [0])            # patched -> ExifIFD
    if with_gps:
        ifd0.add(0x8825, LONG, [0])        # patched -> GPSIFD
    if with_subifd:
        ifd0.add(0x014A, LONG, [0])        # patched -> SubIFD

    ifd1 = Ifd()
    if with_thumbnail:
        ifd1.add(0x0100, LONG, [1])
        ifd1.add(0x0101, LONG, [1])
        ifd1.add(0x0103, SHORT, [6])
        ifd1.add(0x011A, RATIONAL, [rat(72, 1)])
        ifd1.add(0x011B, RATIONAL, [rat(72, 1)])
        ifd1.add(0x0128, SHORT, [2])
        ifd1.add(0x0201, LONG, [0])        # patched -> thumbnail
        ifd1.add(0x0202, LONG, [len(THUMB)])

    def ifd_size(ifd):
        return 2 + 12 * len(ifd.entries) + 4

    def layout_size(offsets):
        pos = 8
        for key, ifd in (("ifd0", ifd0), ("ifd1", ifd1), ("exif", exif), ("gps", gps),
                         ("interop", interop), ("makernote", makernote), ("subifd", subifd)):
            if ifd is None or not ifd.entries:
                offsets[key] = 0
                continue
            offsets[key] = pos
            pos += ifd_size(ifd)
        return pos

    def payload_size(offsets):
        total = 0
        for key, ifd in (("ifd0", ifd0), ("ifd1", ifd1), ("exif", exif), ("gps", gps),
                         ("interop", interop), ("makernote", makernote), ("subifd", subifd)):
            if ifd is None or not ifd.entries:
                continue
            for tag, typ, count, payload in sorted(ifd.entries, key=lambda e: e[0]):
                size = SIZES.get(typ, 1) * count
                if size > 4:
                    total += len(payload)
                    if total % 2:
                        total += 1
        return total

    offsets = {}
    for _ in range(6):
        before = dict(offsets)
        payload_abs = layout_size(offsets)
        thumb_abs = payload_abs + payload_size(offsets)
        # pointer patches do not alter sizes; iterate until the map is stable
        if before == offsets and _:
            break

    def build():
        dirs = []
        payloads = b""
        payload_start = payload_abs
        for key, ifd in (("ifd0", ifd0), ("ifd1", ifd1), ("exif", exif), ("gps", gps),
                         ("interop", interop), ("makernote", makernote), ("subifd", subifd)):
            if ifd is None or not ifd.entries:
                dirs.append((key, b""))
                continue
            blob, extra = ifd_parts(ifd.entries)
            blob = ifd_patch_offsets(blob, ifd.entries, payload_start + len(payloads))
            payloads += extra
            dirs.append((key, blob))
        return dict(dirs), payloads

    dirs, payloads = build()

    def patch_dir(key, tag, value):
        ifd = {"ifd0": ifd0, "ifd1": ifd1, "exif": exif, "gps": gps, "interop": interop,
               "makernote": makernote, "subifd": subifd}[key]
        if ifd is None or not ifd.entries or not dirs.get(key):
            return
        order = sorted(ifd.entries, key=lambda e: e[0])
        blob = bytearray(dirs[key])
        for i, e in enumerate(order):
            if e[0] == tag:
                struct.pack_into("<I", blob, 2 + 12 * i + 8, value)
        dirs[key] = bytes(blob)

    patch_dir("ifd0", 0x8769, offsets["exif"])
    if with_gps:
        patch_dir("ifd0", 0x8825, offsets["gps"])
    if with_subifd:
        patch_dir("ifd0", 0x014A, offsets["subifd"])
    patch_dir("exif", 0xA005, offsets["interop"])
    if with_thumbnail:
        patch_dir("ifd1", 0x0201, thumb_abs)

    # IFD0 chains to IFD1 through its trailing next-IFD pointer.
    if with_thumbnail and dirs["ifd0"]:
        blob = bytearray(dirs["ifd0"])
        struct.pack_into("<I", blob, len(blob) - 4, offsets["ifd1"])
        dirs["ifd0"] = bytes(blob)

    body = b""
    for key in ("ifd0", "ifd1", "exif", "gps", "interop", "makernote", "subifd"):
        body += dirs.get(key, b"")
    body += payloads
    blob = header + body
    assert len(blob) == thumb_abs, ("layout drift", len(blob), thumb_abs)
    if with_thumbnail:
        blob += THUMB
    return blob, offsets["ifd0"]



def write_jpeg(path, exif=None, comment=None):
    out = b"\xff\xd8"
    if exif:
        payload = b"Exif\x00\x00" + exif
        out += b"\xff\xe1" + struct.pack(">H", len(payload) + 2) + payload
    if comment:
        out += b"\xff\xfe" + struct.pack(">H", len(comment) + 2) + comment
    out += b"\xff\xdb\x00\x43\x00" + b"\x10" * 64
    out += b"\xff\xc0\x00\x11\x08\x00\x10\x00\x10\x03\x01\x11\x00\x02\x11\x01\x03\x11\x01"
    out += b"\xff\xc4\x00\x1f\x00" + b"\x00" * 29
    out += b"\xff\xda\x00\x08\x01\x01\x00\x00\x3f\x00" + b"\x00\x00\x00" + b"\xff\xd9"
    with open(path, "wb") as f:
        f.write(out)
    return out


def write_png(path, exif):
    import binascii

    def chunk(typ, data):
        body = typ + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", binascii.crc32(body) & 0xFFFFFFFF)

    out = b"\x89PNG\r\n\x1a\n"
    out += chunk(b"IHDR", struct.pack(">IIBBBBB", 16, 16, 8, 2, 0, 0, 0))
    out += chunk(b"eXIf", exif)
    out += chunk(b"IDAT", b"\x78\x9c\x63\x00\x01\x00\x00\x05\x00\x01" + b"\x00" * 4)
    out += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(out)


def write_heif(path, exif):
    def box(typ, payload):
        return struct.pack(">I", len(payload) + 8) + typ + payload

    def full_box(typ, payload):
        return box(typ, b"\x00\x00\x00\x00" + payload)

    item = b"Exif\x00\x00" + exif

    def build(exif_off):
        infe = full_box(b"infe", struct.pack(">HH", 1, 0) + b"Exif" + b"\x00")
        iinf = full_box(b"iinf", struct.pack(">H", 1) + infe)
        iloc_body = bytes([0x44, 0x00]) + struct.pack(">H", 1)
        iloc_body += struct.pack(">HH", 1, 0) + struct.pack(">H", 1)
        iloc_body += struct.pack(">I", exif_off) + struct.pack(">I", len(item))
        iloc = full_box(b"iloc", iloc_body)
        meta = full_box(b"meta", iinf + iloc)
        ftyp = box(b"ftyp", b"heic" + struct.pack(">I", 0) + b"mif1heic")
        return ftyp + meta

    off = len(build(0))
    head = build(off)
    assert len(head) == off, (len(head), off)
    with open(path, "wb") as f:
        f.write(head + item)


def le_ifd(entries, offset_base=0):
    """Serialize a little-endian IFD (used for MakerNote payloads). Out-of-line
    values follow the directory; offset_base is added to their offsets so the
    result can be embedded after a TIFF header."""
    entries = sorted(entries, key=lambda e: e[0])
    dir_len = 2 + 12 * len(entries) + 4
    out = struct.pack("<H", len(entries))
    extra = b""
    for tag, typ, count, payload in entries:
        size = SIZES.get(typ, 1) * count
        if size <= 4:
            out += struct.pack("<HHI", tag, typ, count) + payload.ljust(4, bytes([0]))
        else:
            off = offset_base + dir_len + len(extra)
            out += struct.pack("<HHI", tag, typ, count) + struct.pack("<I", off)
            extra += payload
    out += struct.pack("<I", 0)
    return out + extra


def build_with_makernote(mn_bytes, make):
    """Minimal EXIF block whose Exif IFD carries the given MakerNote blob."""
    thumb = THUMB
    ifd1 = Ifd()
    ifd1.add(0x0201, LONG, [0])
    ifd1.add(0x0202, LONG, [len(thumb)])
    exif = Ifd()
    exif.add(0x9003, ASCII, "2024:05:17 18:23:45")
    exif.add(0x927C, UNDEFINED, None, raw=mn_bytes)
    ifd0 = Ifd()
    ifd0.add(0x010F, ASCII, make)
    ifd0.add(0x0110, ASCII, "TEST")
    ifd0.add(0x8769, LONG, [0])

    def size(ifd):
        return 2 + 12 * len(ifd.entries) + 4

    abs0 = 8
    abs1 = abs0 + size(ifd0)
    abse = abs1 + size(ifd1)
    payload_abs = abse + size(exif)
    blobs = {}
    payloads = b""
    for key, ifd in (("ifd0", ifd0), ("ifd1", ifd1), ("exif", exif)):
        blob, extra = ifd_parts(ifd.entries)
        blobs[key] = ifd_patch_offsets(blob, ifd.entries, payload_abs + len(payloads))
        payloads += extra
    thumb_abs = payload_abs + len(payloads)
    for key, ifd in (("ifd0", ifd0), ("ifd1", ifd1), ("exif", exif)):
        order = sorted(ifd.entries, key=lambda e: e[0])
        b = bytearray(blobs[key])
        for i, e in enumerate(order):
            if e[0] == 0x8769:
                struct.pack_into("<I", b, 2 + 12 * i + 8, abse)
            if e[0] == 0x0201:
                struct.pack_into("<I", b, 2 + 12 * i + 8, thumb_abs)
        blobs[key] = bytes(b)
    b = bytearray(blobs["ifd0"])
    struct.pack_into("<I", b, len(b) - 4, abs1)
    blobs["ifd0"] = bytes(b)
    blob = b"II*" + bytes([0]) + struct.pack("<I", 8) + blobs["ifd0"] + blobs["ifd1"] + blobs["exif"] + payloads
    assert len(blob) == thumb_abs, (len(blob), thumb_abs)
    return blob + thumb


def write_makernote_fixtures():
    """Two MakerNote layouts: Nikon type 3 (embedded TIFF header) and a
    vendor that stores a plain IFD at the start of the block (Sony)."""
    # Out-of-line values are offset past the 8-byte nested TIFF header.
    inner = le_ifd([
        (0x0002, SHORT, 1, struct.pack("<H", 400)),
        (0x0004, ASCII, 14, b"FINE          "),
        (0x00A7, LONG, 1, struct.pack("<I", 4242)),
    ], offset_base=8)
    # Nikon type 3 header: "Nikon\0" + version 2.0 + 2 pad bytes, then a nested TIFF header.
    nikon = b"Nikon" + bytes([0, 2, 0, 0, 0]) + b"II*" + bytes([0]) + struct.pack("<I", 8) + inner
    write_jpeg(os.path.join(OUT, "makernote_nikon3.jpg"),
               build_with_makernote(nikon, b"NIKON CORPORATION"))

    sony = le_ifd([
        (0x0102, SHORT, 1, struct.pack("<H", 2)),
        (0x0115, SHORT, 1, struct.pack("<H", 1)),
        (0x2010, ASCII, 12, b"ILCE-7M4" + bytes([0]) * 4),
        (0xB027, LONG, 1, struct.pack("<I", 42)),
    ])
    write_jpeg(os.path.join(OUT, "makernote_sony.jpg"), build_with_makernote(sony, b"SONY"))


def ifd0_entry_count(tiff_bytes):
    import struct as _s
    return _s.unpack_from("<H", tiff_bytes, 8)[0]


def main():
    exif, _ = build_tiff()
    write_jpeg(os.path.join(OUT, "sample.jpg"), exif)
    exif2, _ = build_tiff(with_makernote=False)
    write_jpeg(os.path.join(OUT, "sample_inline.jpg"), exif2)
    exif3, _ = build_tiff(with_gps=False, with_makernote=False)
    write_jpeg(os.path.join(OUT, "sample_nogps.jpg"), exif3)
    exif4, _ = build_tiff(with_subifd=True)
    write_jpeg(os.path.join(OUT, "sample_subifd.jpg"), exif4)
    write_png(os.path.join(OUT, "sample.png"), exif)
    with open(os.path.join(OUT, "sample.tif"), "wb") as f:
        f.write(build_tiff()[0])
    write_heif(os.path.join(OUT, "sample.heic"), exif)

    write_makernote_fixtures()

    write_jpeg(os.path.join(OUT, "nometa.jpg"), None, comment=b"just a comment")

    data = open(os.path.join(OUT, "sample.jpg"), "rb").read()
    with open(os.path.join(OUT, "truncated.jpg"), "wb") as f:
        f.write(data[:len(data) // 2])
    with open(os.path.join(OUT, "header_only.jpg"), "wb") as f:
        f.write(data[:6])

    # IFD0 next-pointer that loops back onto itself.
    raw, _ = build_tiff(with_thumbnail=False)
    bad = bytearray(raw)
    n_entries = ifd0_entry_count(raw)
    struct.pack_into("<I", bad, 8 + 2 + 12 * n_entries, 8)
    with open(os.path.join(OUT, "cycle.tif"), "wb") as f:
        f.write(bytes(bad))

    # Absurd declared entry count inside IFD0.
    bad2 = bytearray(build_tiff(with_thumbnail=False)[0])
    struct.pack_into("<H", bad2, 8, 0xFFFF)
    with open(os.path.join(OUT, "badcount.tif"), "wb") as f:
        f.write(bytes(bad2))

    # Copyright payload offset far outside the file.
    raw3, _ = build_tiff(with_thumbnail=False)
    bad3 = bytearray(raw3)
    n3 = struct.unpack_from("<H", bad3, 8)[0]
    for i in range(n3):
        e = 8 + 2 + 12 * i
        if struct.unpack_from("<H", bad3, e)[0] == 0x010F:   # Make, an out-of-line ASCII value
            struct.pack_into("<I", bad3, e + 8, 0x7FFFFF00)  # far outside the file
    with open(os.path.join(OUT, "badpointer.tif"), "wb") as f:
        f.write(bytes(bad3))

    # Big-endian TIFF with a small dictionary.
    be = b"MM\x00\x2a" + struct.pack(">I", 8)
    entries = [(0x010F, ASCII, b"CANON\x00"), (0x0112, SHORT, struct.pack(">H", 6))]
    blob = struct.pack(">H", 2) + struct.pack(">HHI", 0x010F, 2, 6)
    blob += struct.pack(">I", 8 + 2 + 24 + 4)
    blob += struct.pack(">HHI", 0x0112, 3, 1) + struct.pack(">H", 6) + b"\x00\x00"
    blob += struct.pack(">I", 0)
    blob += b"CANON\x00"
    with open(os.path.join(OUT, "bigendian.tif"), "wb") as f:
        f.write(be + blob)

    open(os.path.join(OUT, "empty.bin"), "wb").close()
    with open(os.path.join(OUT, "notimage.bin"), "wb") as f:
        f.write(b"THIS IS DEFINITELY NOT AN IMAGE" * 4)

    print("fixtures written to", OUT)
    for name in sorted(os.listdir(OUT)):
        print("  %-22s %8d bytes" % (name, os.path.getsize(os.path.join(OUT, name))))


if __name__ == "__main__":
    main()
