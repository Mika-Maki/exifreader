// container.cpp - container sniffing: JPEG / PNG / TIFF / HEIF.
#include "container.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace exif {

namespace {

std::string hex(std::size_t v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "0x%zX", v);
    return buf;
}

bool be16(const std::uint8_t* p, std::size_t size, std::size_t off, std::uint16_t& out) {
    if (off + 2 > size) return false;
    out = static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[off]) << 8) | p[off + 1]);
    return true;
}

bool be32(const std::uint8_t* p, std::size_t size, std::size_t off, std::uint32_t& out) {
    if (off + 4 > size) return false;
    out = (static_cast<std::uint32_t>(p[off]) << 24) | (static_cast<std::uint32_t>(p[off + 1]) << 16) |
          (static_cast<std::uint32_t>(p[off + 2]) << 8) | static_cast<std::uint32_t>(p[off + 3]);
    return true;
}

bool fourcc(const std::uint8_t* p, std::size_t size, std::size_t off, const char* tag) {
    if (off + 4 > size) return false;
    return std::equal(tag, tag + 4, p + off);
}

// True when p looks like a TIFF header; also reports the byte order.
bool tiffHeader(const std::uint8_t* p, std::size_t size, Endian& order) {
    if (size < 8) return false;
    if (p[0] == 'I' && p[1] == 'I') order = Endian::Little;
    else if (p[0] == 'M' && p[1] == 'M') order = Endian::Big;
    else return false;
    const std::uint16_t magic = (order == Endian::Little)
        ? static_cast<std::uint16_t>(p[2] | (p[3] << 8))
        : static_cast<std::uint16_t>((p[2] << 8) | p[3]);
    return magic == 42;   // classic TIFF; BigTIFF (43) handled by the parser
}

// ------------------------------------------------------------------- JPEG

void scanJpeg(const std::uint8_t* d, std::size_t n, ContainerInfo& info) {
    info.kind = ContainerKind::Jpeg;
    info.formatDetail = "JPEG";
    bool jfif = false;
    std::string jfifVer;
    std::size_t pos = 2;  // past SOI
    int budget = 8192;
    while (pos + 1 < n && budget-- > 0) {
        if (d[pos] != 0xFF) { ++pos; continue; }
        const std::size_t markerPos = pos;
        while (pos < n && d[pos] == 0xFF) ++pos;   // fill bytes
        if (pos >= n) break;
        const std::uint8_t marker = d[pos++];
        if (marker == 0x00) continue;                     // stuffed byte
        if (marker == 0xD8 || marker == 0x01) continue;   // SOI / TEM
        if (marker >= 0xD0 && marker <= 0xD7) continue;   // RSTn
        if (marker == 0xD9) break;                        // EOI

        std::uint16_t segLen = 0;
        if (!be16(d, n, pos, segLen) || segLen < 2) break;
        const std::size_t payloadOff = pos + 2;
        const std::size_t payloadLen = segLen - 2;
        if (payloadOff > n || payloadLen > n - payloadOff) break;   // truncated

        const std::uint8_t* seg = d + payloadOff;
        if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC) {
            if (payloadLen >= 5) {
                info.height = static_cast<std::uint32_t>((seg[1] << 8) | seg[2]);
                info.width = static_cast<std::uint32_t>((seg[3] << 8) | seg[4]);
                static const char* names[] = {"baseline",     "extended sequential", "progressive",
                                              "lossless",     "differential sequential",
                                              "differential progressive", "differential lossless"};
                const unsigned idx = marker - 0xC0;
                info.frameType = (idx < 7) ? names[idx] : "reserved";
                char buf[48];
                std::snprintf(buf, sizeof(buf), "SOF%u", marker - 0xC0);
                info.frameType += " ";
                info.frameType += buf;
            }
        } else if (marker == 0xE1 && payloadLen >= 6 && std::equal(seg, seg + 6, "Exif\0\0")) {
            EmbeddedExif ee;
            ee.found = true;
            ee.payloadOffset = payloadOff + 6;
            ee.payloadSize = n - ee.payloadOffset;
            ee.note = "APP1 Exif segment at " + hex(markerPos);
            info.exifCandidates.push_back(ee);
        } else if (marker == 0xE1 && payloadLen >= 29 &&
                   std::equal(seg, seg + 29, "http://ns.adobe.com/xap/1.0/")) {
            info.hasXmp = true;
        } else if (marker == 0xE0 && payloadLen >= 7 && std::equal(seg, seg + 5, "JFIF\0")) {
            jfif = true;
            jfifVer = std::to_string(seg[5]) + "." + std::to_string(seg[6]);
        } else if (marker == 0xE0 && payloadLen >= 6 && std::equal(seg, seg + 5, "JFXX\0")) {
            jfif = true;
        }

        if (marker == 0xDA) break;   // start of scan: entropy-coded data follows
        pos = payloadOff + payloadLen;
    }
    if (jfif) info.formatDetail = "JPEG (JFIF " + (jfifVer.empty() ? "1.0" : jfifVer) + ")";
    if (info.hasXmp) info.formatDetail += " + XMP";
}

// -------------------------------------------------------------------- PNG

void scanPng(const std::uint8_t* d, std::size_t n, ContainerInfo& info) {
    info.kind = ContainerKind::Png;
    info.formatDetail = "PNG";
    std::size_t pos = 8;   // past signature
    int budget = 100000;
    while (pos + 8 <= n && budget-- > 0) {
        std::uint32_t len = 0;
        if (!be32(d, n, pos, len)) break;
        const std::size_t typeOff = pos + 4;
        const std::size_t dataOff = pos + 8;
        if (dataOff > n || len > n - dataOff) break;    // truncated / absurd
        if (fourcc(d, n, typeOff, "IHDR") && len >= 8) {
            be32(d, n, dataOff, info.width);
            be32(d, n, dataOff + 4, info.height);
            if (len >= 13) {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "IHDR %u-bit %s, %s", d[dataOff + 8],
                              d[dataOff + 9] == 0 ? "grayscale"
                                                  : d[dataOff + 9] == 2 ? "RGB"
                                                                        : d[dataOff + 9] == 3 ? "palette"
                                                                                              : d[dataOff + 9] == 4 ? "gray+alpha" : "RGBA",
                              d[dataOff + 12] == 0 ? "non-interlaced" : "Adam7 interlaced");
                info.frameType = buf;
            }
        } else if (fourcc(d, n, typeOff, "eXIf")) {
            Endian order = Endian::Little;
            if (tiffHeader(d + dataOff, len, order)) {
                EmbeddedExif ee;
                ee.found = true;
                ee.payloadOffset = dataOff;
                ee.payloadSize = len;
                ee.note = "PNG eXIf chunk at " + hex(pos) + " (" +
                          (order == Endian::Little ? "little" : "big") + " endian)";
                info.exifCandidates.push_back(ee);
            }
        }
        pos = dataOff + static_cast<std::size_t>(len) + 4;   // + CRC
    }
}

// ------------------------------------------------------------- HEIF / AVIF

struct Box {
    std::size_t start = 0;     // offset of the box header
    std::size_t content = 0;   // offset of the payload
    std::size_t end = 0;       // one past the payload (clamped to buffer)
    std::string type;
};

bool readBox(const std::uint8_t* d, std::size_t n, std::size_t off, Box& box) {
    std::uint32_t size32 = 0;
    if (!be32(d, n, off, size32)) return false;
    if (off + 8 > n) return false;
    box.start = off;
    box.type.assign(reinterpret_cast<const char*>(d + off + 4), 4);
    std::size_t headerSize = 8;
    std::size_t boxSize = size32;
    if (size32 == 1) {
        std::uint32_t hi = 0, lo = 0;
        if (!be32(d, n, off + 8, hi) || !be32(d, n, off + 12, lo)) return false;
        boxSize = (static_cast<std::size_t>(hi) << 32) | static_cast<std::size_t>(lo);
        headerSize = 16;
    } else if (size32 == 0) {
        boxSize = n - off;   // box extends to end of file
    }
    if (boxSize < headerSize) return false;
    box.content = off + headerSize;
    box.end = off + std::min(boxSize, n - off);
    return box.end >= box.content;
}

bool findChild(const std::uint8_t* d, std::size_t n, const Box& parent, const char* type, Box& out) {
    std::size_t pos = parent.content;
    int budget = 100000;
    while (pos + 8 <= parent.end && budget-- > 0) {
        Box b;
        if (!readBox(d, n, pos, b)) return false;
        if (b.type == type) { out = b; return true; }
        if (b.end <= pos) return false;
        pos = b.end;
    }
    return false;
}

std::uint64_t readSized(const std::uint8_t* d, std::size_t n, std::size_t off, std::size_t sz, bool& ok) {
    if (sz == 0) return 0;
    if (sz > 8 || off + sz > n) { ok = false; return 0; }
    std::uint64_t v = 0;
    for (std::size_t i = 0; i < sz; ++i) v = (v << 8) | d[off + i];
    return v;
}

void scanHeif(const std::uint8_t* d, std::size_t n, ContainerInfo& info) {
    info.kind = ContainerKind::Heif;
    info.formatDetail = "ISO-BMFF/HEIF";
    std::size_t pos = 0;
    Box meta;
    bool haveMeta = false;
    int budget = 100000;
    while (pos + 8 <= n && budget-- > 0) {
        Box b;
        if (!readBox(d, n, pos, b)) break;
        if (b.type == "ftyp" && b.content + 4 <= n) {
            info.formatDetail += " (major brand " +
                std::string(reinterpret_cast<const char*>(d + b.content), 4) + ")";
        } else if (b.type == "meta") {
            meta = b;
            haveMeta = true;
            break;
        }
        if (b.end <= pos) break;
        pos = b.end;
    }
    if (!haveMeta) return;

    Box metaChildren = meta;          // meta is a FullBox
    metaChildren.content += 4;        // skip version/flags
    if (metaChildren.content > metaChildren.end) return;

    // 1) find the item id whose infe type is 'Exif'
    std::uint32_t exifItemId = 0;
    Box iinf;
    if (findChild(d, n, metaChildren, "iinf", iinf)) {
        std::uint16_t version = 0;
        if (be16(d, n, iinf.content, version)) {
            std::size_t p = iinf.content + 4 + (version == 0 ? 2 : 4);   // version/flags + entry_count
            int b2 = 100000;
            while (p + 8 <= iinf.end && b2-- > 0) {
                Box infe;
                if (!readBox(d, n, p, infe)) break;
                if (infe.type == "infe" && infe.content + 4 <= infe.end) {
                    const std::uint8_t v = d[infe.content];
                    std::uint32_t itemId = 0;
                    std::string itemType;
                    if (v == 2) {
                        std::uint16_t id16 = 0;
                        if (be16(d, n, infe.content + 4, id16)) itemId = id16;
                        if (infe.content + 12 <= infe.end)
                            itemType.assign(reinterpret_cast<const char*>(d + infe.content + 8), 4);
                    } else if (v >= 3) {
                        if (!be32(d, n, infe.content + 4, itemId)) itemId = 0;
                        if (infe.content + 16 <= infe.end)
                            itemType.assign(reinterpret_cast<const char*>(d + infe.content + 12), 4);
                    }
                    if (itemType == "Exif" && exifItemId == 0) exifItemId = itemId;
                }
                if (infe.end <= p) break;
                p = infe.end;
            }
        }
    }
    if (exifItemId == 0) return;

    // 2) resolve its byte extent through iloc
    Box iloc;
    if (!findChild(d, n, metaChildren, "iloc", iloc)) return;
    if (iloc.content + 8 > iloc.end) return;
    const std::uint8_t version = d[iloc.content];
    std::uint8_t offsetSize = d[iloc.content + 4];
    std::uint8_t lengthSize = d[iloc.content + 5];
    std::uint8_t baseOffsetSize = d[iloc.content + 6];
    std::uint8_t indexSize = 0;
    if (version == 1 || version == 2) {
        indexSize = static_cast<std::uint8_t>(baseOffsetSize & 0x0F);
        baseOffsetSize = static_cast<std::uint8_t>(baseOffsetSize >> 4);
    }
    std::size_t cur = iloc.content + (version < 2 ? 8 : 10);
    std::uint32_t itemCount = 0;
    if (version < 2) {
        std::uint16_t c16 = 0;
        if (!be16(d, n, cur, c16)) return;
        itemCount = c16;
        cur += 2;
    } else {
        if (!be32(d, n, cur, itemCount)) return;
        cur += 4;
    }
    for (std::uint32_t i = 0; i < itemCount && cur < iloc.end; ++i) {
        std::uint32_t id = 0;
        if (version < 2) {
            std::uint16_t id16 = 0;
            if (!be16(d, n, cur, id16)) return;
            id = id16;
            cur += 2;
        } else {
            if (!be32(d, n, cur, id)) return;
            cur += 4;
        }
        if (version == 1 || version == 2) cur += 2;   // construction_method
        cur += 2;                                     // data_reference_index
        std::uint16_t extentCount = 0;
        if (!be16(d, n, cur, extentCount)) return;
        cur += 2;
        for (std::uint16_t e = 0; e < extentCount; ++e) {
            bool ok = true;
            const std::uint64_t baseOffset = readSized(d, n, cur, baseOffsetSize, ok);
            cur += baseOffsetSize;
            if (version == 1 || version == 2) cur += indexSize;
            const std::uint64_t extentOffset = readSized(d, n, cur, offsetSize, ok);
            cur += offsetSize;
            const std::uint64_t extentLength = readSized(d, n, cur, lengthSize, ok);
            cur += lengthSize;
            if (!ok) return;
            if (id != exifItemId || extentLength == 0) continue;

            std::size_t off = static_cast<std::size_t>(baseOffset + extentOffset);
            std::size_t len = static_cast<std::size_t>(extentLength);
            if (off >= n) continue;
            len = std::min(len, n - off);

            // The Exif item payload is: 4-byte offset to the TIFF header,
            // then "Exif\0\0", then the header itself.
            Endian order = Endian::Little;
            std::size_t tiffOff = off;
            if (len > 10 && std::equal(d + off + 4, d + off + 10, "Exif\0\0")) {
                tiffOff = off + 10;
            } else if (len > 4) {
                std::uint32_t skip = 0;
                be32(d, n, off, skip);
                tiffOff = off + 4 + skip;
            }
            if (tiffOff < off || tiffOff >= n) continue;
            if (!tiffHeader(d + tiffOff, n - tiffOff, order)) continue;
            EmbeddedExif ee;
            ee.found = true;
            ee.payloadOffset = tiffOff;
            ee.payloadSize = n - tiffOff;
            ee.note = "HEIF Exif item " + std::to_string(id) + " (offset " + hex(off) + ")";
            info.exifCandidates.push_back(ee);
        }
    }
}

}  // namespace

const char* containerName(ContainerKind k) {
    switch (k) {
        case ContainerKind::Jpeg: return "JPEG";
        case ContainerKind::Png: return "PNG";
        case ContainerKind::Tiff: return "TIFF";
        case ContainerKind::Heif: return "HEIF";
        default: return "unknown";
    }
}

ContainerInfo detectContainer(const std::uint8_t* d, std::size_t n) {
    ContainerInfo info;
    if (n < 4) return info;

    if (d[0] == 0xFF && d[1] == 0xD8) {
        scanJpeg(d, n, info);
        return info;
    }
    static const std::uint8_t pngSig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (n >= 8 && std::equal(pngSig, pngSig + 8, d)) {
        scanPng(d, n, info);
        return info;
    }
    const bool hasMagic = n >= 4 &&
        ((d[0] == 'I' && d[1] == 'I') || (d[0] == 'M' && d[1] == 'M'));
    if (hasMagic && n >= 8) {
        const std::uint16_t magic = (d[0] == 'I')
            ? static_cast<std::uint16_t>(d[2] | (d[3] << 8))
            : static_cast<std::uint16_t>((d[2] << 8) | d[3]);
        if (magic == 42 || magic == 43) {
            info.kind = ContainerKind::Tiff;
            info.formatDetail = std::string("TIFF (raw, ") +
                (d[0] == 'I' ? "little" : "big") + " endian" +
                (magic == 42 ? ", classic)" : ", BigTIFF)");
            EmbeddedExif ee;
            ee.found = true;
            ee.payloadOffset = 0;
            ee.payloadSize = n;
            ee.note = "file header";
            info.exifCandidates.push_back(ee);
            return info;
        }
    }
    if (fourcc(d, n, 4, "ftyp")) {
        scanHeif(d, n, info);
        return info;
    }
    return info;
}

}  // namespace exif
