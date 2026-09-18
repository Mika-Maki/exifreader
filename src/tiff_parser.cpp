// tiff_parser.cpp - EXIF/TIFF IFD tree parser with bounds and cycle safety.
//
// Offset model
// ------------
// A directory or tag value has a stored offset (as written in the file,
// relative to the TIFF header) and a file offset (absolute inside the buffer).
// readAt() maps stored -> buffer index; absOf() maps stored -> file offset.
// Vendor MakerNote blocks carrying their own TIFF header are parsed from a
// self-contained copy with blobDelta_ set, so their internal offsets resolve
// while reported offsets stay in file coordinates.
#include "tiff_parser.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

#include "tiff_tags.hpp"

namespace exif {

int typeSize(std::uint16_t type) {
    switch (type) {
        case 1: case 2: case 6: case 7: return 1;
        case 3: case 8: return 2;
        case 4: case 9: case 11: case 13: return 4;
        case 5: case 10: case 12: return 8;
        case 16: case 17: case 18: return 8;   // BigTIFF LONG8/SLONG8/IFD8
        default: return 0;
    }
}

const char* typeName(std::uint16_t type) {
    switch (type) {
        case 1: return "BYTE";
        case 2: return "ASCII";
        case 3: return "SHORT";
        case 4: return "LONG";
        case 5: return "RATIONAL";
        case 6: return "SBYTE";
        case 7: return "UNDEFINED";
        case 8: return "SSHORT";
        case 9: return "SLONG";
        case 10: return "SRATIONAL";
        case 11: return "FLOAT";
        case 12: return "DOUBLE";
        case 13: return "IFD";
        case 16: return "LONG8";
        case 17: return "SLONG8";
        case 18: return "IFD8";
        default: return "?";
    }
}

bool isAsciiType(std::uint16_t t) { return t == 2; }
bool isRationalType(std::uint16_t t) { return t == 5 || t == 10; }
std::size_t valueCount(const TagEntry& e) { return static_cast<std::size_t>(e.count); }

const char* ifdKindName(IfdKind k) {
    switch (k) {
        case IfdKind::Ifd0: return "IFD0";
        case IfdKind::Ifd1: return "IFD1";
        case IfdKind::Exif: return "Exif";
        case IfdKind::Gps: return "GPS";
        case IfdKind::Interop: return "Interop";
        case IfdKind::MakerNote: return "MakerNote";
        default: return "IFD";
    }
}

namespace {

bool valueAt(const TagEntry& e, Endian endian, std::size_t index, double& out, bool& isRational) {
    const int ts = typeSize(e.type);
    if (ts == 0 || index >= e.count) return false;
    const std::size_t off = index * static_cast<std::size_t>(ts);
    if (off + static_cast<std::size_t>(ts) > e.raw.size()) return false;
    const std::uint8_t* p = e.raw.data() + off;
    isRational = false;
    switch (e.type) {
        case 1: case 7: out = p[0]; return true;
        case 6: out = static_cast<double>(static_cast<std::int8_t>(p[0])); return true;
        case 3: out = static_cast<double>(readUInt(p, 2, endian)); return true;
        case 8: out = static_cast<double>(readInt(p, 2, endian)); return true;
        case 4: case 13: out = static_cast<double>(readUInt(p, 4, endian)); return true;
        case 9: out = static_cast<double>(readInt(p, 4, endian)); return true;
        case 11: {
            const std::uint32_t bits = readUInt(p, 4, endian);
            float f = 0.0f;
            std::memcpy(&f, &bits, sizeof(f));
            out = static_cast<double>(f);
            return true;
        }
        case 12: {
            std::uint64_t bits = 0;
            if (endian == Endian::Little) {
                for (int i = 7; i >= 0; --i) bits = (bits << 8) | p[i];
            } else {
                for (int i = 0; i < 8; ++i) bits = (bits << 8) | p[i];
            }
            double d = 0.0;
            std::memcpy(&d, &bits, sizeof(d));
            out = d;
            return true;
        }
        case 5: out = static_cast<double>(readUInt(p, 4, endian)); isRational = true; return true;
        case 10: out = static_cast<double>(readInt(p, 4, endian)); isRational = true; return true;
        default: return false;
    }
}

bool denominatorAt(const TagEntry& e, Endian endian, std::size_t index, double& out) {
    if (!isRationalType(e.type)) return false;
    const std::size_t off = index * 8 + 4;
    if (off + 4 > e.raw.size()) return false;
    const std::uint8_t* p = e.raw.data() + off;
    out = (e.type == 5) ? static_cast<double>(readUInt(p, 4, endian))
                        : static_cast<double>(readInt(p, 4, endian));
    return true;
}

std::string hexOf(std::size_t v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%zX", v);
    return buf;
}

std::string hexBytes(const std::vector<std::uint8_t>& v, std::size_t maxBytes) {
    static const char* digits = "0123456789abcdef";
    std::string s;
    const std::size_t n = std::min(v.size(), maxBytes);
    for (std::size_t i = 0; i < n; ++i) {
        if (i) s += ' ';
        s += digits[v[i] >> 4];
        s += digits[v[i] & 0xF];
    }
    if (v.size() > n) s += " ...";
    return s;
}

// F7: UTF-8 aware escaping shared with the renderer (exif_common.hpp).
// Bytes >= 0x80 that are not valid UTF-8 become U+FFFD, so --json output is
// always decodable regardless of what the tag payload contains.
std::string jsonEscape(const std::string& s) { return escapeJson(s); }

std::string trimNul(const std::string& s) {
    std::string t = s;
    while (!t.empty() && (t.back() == '\0' || t.back() == ' ')) t.pop_back();
    return t;
}

// A double is only convertible to long long inside this range; anything
// outside it (and NaN/Inf) would be undefined behaviour.
const double kLLMax = 9223372036854775808.0;   // 2^63

long long safeLL(double v) {
    if (!std::isfinite(v) || v >= kLLMax || v < -kLLMax) return 0;
    return static_cast<long long>(v);
}

std::string num(double v) {
    // F4: NaN/Inf and out-of-range values must never reach a float->int cast.
    if (std::isnan(v)) return "nan";
    if (std::isinf(v)) return v < 0 ? "-inf" : "inf";
    char buf[64];
    if (v > -1e15 && v < 1e15 && v == static_cast<double>(static_cast<long long>(v))) {
        std::snprintf(buf, sizeof(buf), "%.0f", v);
        return buf;
    }
    if (v != 0.0 && (v < 1e-4 || v > 1e7)) {
        std::snprintf(buf, sizeof(buf), "%.6g", v);
        return buf;
    }
    std::snprintf(buf, sizeof(buf), "%.6f", v);
    std::string s(buf);
    while (s.size() > 1 && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s;
}

// A numeric JSON fragment. Non-finite values are not valid JSON numbers, so
// they are emitted as strings instead of bare nan/inf tokens.
std::string jsonNum(double v) {
    if (std::isfinite(v)) return num(v);
    return std::isnan(v) ? "\"nan\"" : (v < 0 ? "\"-inf\"" : "\"inf\"");
}

std::string enumMeaning(std::uint16_t tag, long long v) {
    switch (tag) {
        case 0x0112:
            switch (v) {
                case 1: return "normal";
                case 2: return "mirror horizontal";
                case 3: return "rotate 180";
                case 4: return "mirror vertical";
                case 5: return "mirror horizontal + rotate 270";
                case 6: return "rotate 90 CW";
                case 7: return "mirror horizontal + rotate 90";
                case 8: return "rotate 270 CW";
                default: return "";
            }
        case 0x0128: return v == 1 ? "none" : v == 2 ? "inch" : v == 3 ? "cm" : "";
        case 0xA001: return v == 1 ? "sRGB" : v == 0xFFFF ? "uncalibrated" : "";
        case 0x0103: return v == 1 ? "uncompressed" : v == 6 ? "JPEG (old-style)" : v == 7 ? "JPEG" : "";
        case 0x0213: return v == 1 ? "centered" : v == 2 ? "co-sited" : "";
        case 0x9207:
            switch (v) {
                case 0: return "unknown";
                case 1: return "average";
                case 2: return "center-weighted average";
                case 3: return "spot";
                case 4: return "multi-spot";
                case 5: return "pattern";
                case 6: return "partial";
                default: return "";
            }
        case 0x9208:
            switch (v) {
                case 0: return "unknown";
                case 1: return "daylight";
                case 2: return "fluorescent";
                case 3: return "tungsten";
                case 4: return "flash";
                case 9: return "fine weather";
                case 10: return "cloudy";
                case 11: return "shade";
                default: return "";
            }
        case 0x8822:
            switch (v) {
                case 0: return "not defined";
                case 1: return "manual";
                case 2: return "normal program";
                case 3: return "aperture priority";
                case 4: return "shutter priority";
                case 5: return "creative program";
                case 6: return "action program";
                case 7: return "portrait mode";
                case 8: return "landscape mode";
                default: return "";
            }
        case 0xA402: return v == 0 ? "auto" : v == 1 ? "manual" : v == 2 ? "auto bracket" : "";
        case 0xA403: return v == 0 ? "auto" : v == 1 ? "manual" : "";
        case 0xA406:
            switch (v) {
                case 0: return "standard";
                case 1: return "landscape";
                case 2: return "portrait";
                case 3: return "night";
                default: return "";
            }
        case 0xA401: return v == 0 ? "normal process" : v == 1 ? "custom process" : "";
        case 0xA408: return v == 0 ? "normal" : v == 1 ? "soft" : v == 2 ? "hard" : "";
        case 0xA409: return v == 0 ? "normal" : v == 1 ? "low" : v == 2 ? "high" : "";
        case 0xA40A: return v == 0 ? "normal" : v == 1 ? "soft" : v == 2 ? "hard" : "";
        case 0xA40C: return v == 1 ? "macro" : v == 2 ? "close view" : v == 3 ? "distant view" : "";
        case 0xA300: return v == 3 ? "digital still camera" : "";
        case 0xA301: return v == 1 ? "directly photographed" : "";
        default: return "";
    }
}

std::string flashMeaning(long long v) {
    std::string s = (v & 1) ? "fired" : "did not fire";
    switch ((v >> 1) & 0x3) {
        case 2: s += ", no return detected"; break;
        case 3: s += ", return detected"; break;
        default: break;
    }
    switch ((v >> 3) & 0x3) {
        case 1: s += ", compulsory"; break;
        case 2: s += ", suppressed"; break;
        case 3: s += ", auto"; break;
        default: break;
    }
    if (v & 0x20) s += ", no flash function";
    if (v & 0x40) s += ", red-eye reduction";
    return s;
}

bool isVersionTag(std::uint16_t tag) { return tag == 0x9000 || tag == 0xA000; }

bool isBinaryAsciiTag(std::uint16_t tag) {
    return tag == 0x9C9B || tag == 0x9C9C || tag == 0x9C9D || tag == 0x9C9E || tag == 0x9C9F;
}

std::string utf16leToUtf8(const std::vector<std::uint8_t>& v) {
    std::string out;
    for (std::size_t i = 0; i + 1 < v.size(); i += 2) {
        const std::uint32_t cp = static_cast<std::uint32_t>(v[i] | (v[i + 1] << 8));
        if (cp == 0) break;
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    return out;
}

const std::size_t kMaxListed = 16;

// F3: ceiling on the entry count of a single directory.
const std::size_t kMaxEntriesPerIfd = 1u << 20;

void formatEntryImpl(TagEntry& e, Endian endian, IfdKind kind) {
    const std::size_t ts = static_cast<std::size_t>(typeSize(e.type));

    if (!e.valueResolved) {
        e.value = "<out of range: " + std::to_string(e.valueSize) + " bytes at 0x" +
                  hexOf(e.valueOffsetAbs) + ">";
        e.json = "\"<unresolved>\"";
        return;
    }

    if (kind == IfdKind::Exif && e.tag == 0x9286) {
        std::string text;
        if (e.raw.size() > 8) {
            const char* cs = reinterpret_cast<const char*>(e.raw.data());
            if (std::memcmp(cs, "ASCII\0\0\0", 8) == 0) {
                text = trimNul(std::string(cs + 8, e.raw.size() - 8));
            } else if (std::memcmp(cs, "UNICODE\0", 8) == 0) {
                std::vector<std::uint8_t> sub(e.raw.begin() + 8, e.raw.end());
                if (endian == Endian::Little) {
                    text = utf16leToUtf8(sub);
                } else {
                    std::vector<std::uint8_t> swapped;
                    swapped.reserve(sub.size());
                    for (std::size_t i = 0; i + 1 < sub.size(); i += 2) {
                        swapped.push_back(sub[i + 1]);
                        swapped.push_back(sub[i]);
                    }
                    text = utf16leToUtf8(swapped);
                }
            } else if (std::memcmp(cs, "JIS\0\0\0\0\0", 8) == 0) {
                text = "<JIS-encoded comment, " + std::to_string(e.raw.size() - 8) + " bytes>";
            } else {
                text = trimNul(std::string(cs, e.raw.size()));
            }
        }
        e.value = text.empty() ? "<empty>" : text;
        e.json = "\"" + jsonEscape(e.value) + "\"";
        return;
    }

    if (isBinaryAsciiTag(e.tag)) {
        const std::string text = utf16leToUtf8(e.raw);
        e.value = text.empty() ? "<empty>" : text;
        e.json = "\"" + jsonEscape(e.value) + "\"";
        return;
    }

    if (e.type == 7) {
        if (isVersionTag(e.tag)) {
            const std::string s = trimNul(std::string(reinterpret_cast<const char*>(e.raw.data()),
                                                      std::min<std::size_t>(e.raw.size(), 4)));
            e.value = s;
            e.json = "\"" + jsonEscape(s) + "\"";
            return;
        }
        if (e.tag == 0x9101 && e.raw.size() >= 4) {
            static const char* names[7] = {"-", "Y", "Cb", "Cr", "R", "G", "B"};
            std::string s;
            for (std::size_t i = 0; i < 4; ++i) {
                if (i) s += " ";
                s += e.raw[i] < 7 ? names[e.raw[i]] : "?";
            }
            e.value = s;
            e.json = "\"" + jsonEscape(s) + "\"";
            return;
        }
        e.value = hexBytes(e.raw, e.raw.size());
        e.json = "\"" + jsonEscape(e.value) + "\"";
        return;
    }

    if (isAsciiType(e.type)) {
        std::string text(reinterpret_cast<const char*>(e.raw.data()), e.raw.size());
        text = trimNul(text);
        if (text.empty()) text = "<empty>";
        e.value = text;
        e.json = "\"" + jsonEscape(text) + "\"";
        return;
    }

    if (ts == 0) {
        e.value = hexBytes(e.raw, 32);
        e.json = "\"" + jsonEscape(e.value) + "\"";
        e.status = "unknown field type " + std::to_string(e.type);
        return;
    }

    const std::size_t total = static_cast<std::size_t>(e.count);
    const std::size_t listed = std::min<std::size_t>(total, kMaxListed);
    std::string val;
    std::string js;
    bool rationalSeen = false;

    for (std::size_t i = 0; i < listed; ++i) {
        double v = 0.0;
        bool isRat = false;
        if (!valueAt(e, endian, i, v, isRat)) break;
        std::string piece;
        std::string jpiece;
        if (isRat) {
            double den = 1.0;
            denominatorAt(e, endian, i, den);
            rationalSeen = true;
            const double dv = (den != 0.0) ? v / den : 0.0;
            if (den == 0.0) {
                piece = (v == 0.0) ? "0/0" : num(v) + "/0";
            } else if (total == 1) {
                piece = num(dv);
                if (dv != v) {
                    char buf[64];
                    std::snprintf(buf, sizeof(buf), " (%.0f/%.0f)", v, den);
                    piece += buf;
                }
            } else {
                piece = num(v);
                if (den != 1.0) {
                    char buf[64];
                    std::snprintf(buf, sizeof(buf), " (%.0f/%.0f)", v, den);
                    piece += buf;
                }
            }
            jpiece = "[" + jsonNum(v) + "," + jsonNum(den) + "]";
        } else if (e.type == 11 || e.type == 12) {
            piece = num(v);
            jpiece = jsonNum(v);
        } else {
            const long long ll = safeLL(v);
            char buf[48];
            if (kind == IfdKind::Gps || e.tag == 0x0000) {
                std::snprintf(buf, sizeof(buf), "%lld", ll);
            } else if (ll >= -9 && ll <= 255) {
                std::snprintf(buf, sizeof(buf), "%lld", ll);
            } else {
                std::snprintf(buf, sizeof(buf), "%lld (0x%llX)", ll, static_cast<unsigned long long>(ll));
            }
            piece = buf;
            jpiece = std::to_string(ll);
        }
        if (i) { val += ", "; js += ","; }
        val += piece;
        js += jpiece;
    }
    if (total > listed) val += ", ... (+" + std::to_string(total - listed) + " more)";
    if (total == 0) val = "<empty>";

    if (total == 1 && !rationalSeen && e.type <= 9) {
        double v = 0.0;
        bool isRat = false;
        if (valueAt(e, endian, 0, v, isRat)) {
            const long long ll = safeLL(v);
            const std::string m = (e.tag == 0x9209) ? flashMeaning(ll) : enumMeaning(e.tag, ll);
            if (!m.empty()) val += "  [" + m + "]";
        }
    }

    e.value = val;
    if (total > listed) e.json = "[" + js + ",\"...\"]";
    else if (listed > 1 || total > 1) e.json = "[" + js + "]";
    else e.json = js;
}

// F7: human-readable output is rendered through escapeForDisplay() so control
// bytes in a tag payload cannot inject newlines or terminal escape sequences.
void formatEntry(TagEntry& e, Endian endian, IfdKind kind) {
    formatEntryImpl(e, endian, kind);
    e.value = escapeForDisplay(e.value);
}

std::string detectMaker(const std::string& make, const std::uint8_t* d, std::size_t n) {
    if (n >= 10 && std::memcmp(d, "OLYMPUS\0II", 10) == 0) return "Olympus";
    if (n >= 8 && std::memcmp(d, "OLYMPUS\0", 8) == 0) return "Olympus";
    if (n >= 8 && std::memcmp(d, "FUJIFILM", 8) == 0) return "Fujifilm";
    if (n >= 12 && std::memcmp(d, "Panasonic\0\0\0", 12) == 0) return "Panasonic";
    if (n >= 10 && std::memcmp(d, "Apple iOS\0", 10) == 0) return "Apple";
    if (n >= 6 && std::memcmp(d, "Nikon\0", 6) == 0) return "Nikon";
    if (n >= 9 && std::memcmp(d, "Panasonic", 9) == 0) return "Panasonic";
    if (n >= 8 && std::memcmp(d, "SONY DSC", 8) == 0) return "Sony";
    if (n >= 4 && std::memcmp(d, "SONY", 4) == 0) return "Sony";
    if (n >= 4 && std::memcmp(d, "AOC\0", 4) == 0) return "Pentax";
    if (n >= 6 && std::memcmp(d, "SAMSUNG", 6) == 0) return "Samsung";
    if (n >= 5 && std::memcmp(d, "LEICA", 5) == 0) return "Leica";
    if (n >= 7 && std::memcmp(d, "Google\0", 7) == 0) return "Google";
    if (n >= 4 && std::memcmp(d, "DJI", 3) == 0) return "DJI";

    const std::string m = toLower(make);
    if (m.find("canon") != std::string::npos) return "Canon";
    if (m.find("nikon") != std::string::npos) return "Nikon";
    if (m.find("sony") != std::string::npos) return "Sony";
    if (m.find("apple") != std::string::npos) return "Apple";
    if (m.find("samsung") != std::string::npos) return "Samsung";
    if (m.find("panasonic") != std::string::npos) return "Panasonic";
    if (m.find("olympus") != std::string::npos) return "Olympus";
    if (m.find("fujifilm") != std::string::npos || m.find("fuji") != std::string::npos) return "Fujifilm";
    if (m.find("pentax") != std::string::npos || m.find("ricoh") != std::string::npos) return "Pentax";
    if (m.find("leica") != std::string::npos) return "Leica";
    if (m.find("google") != std::string::npos) return "Google";
    return "";
}

class Parser {
public:
    Parser(const std::uint8_t* buf, std::size_t size, const ParseOptions& opt, ParseResult& out)
        : r_(buf, size), opt_(opt), res_(out) {}

    void setMakeHint(const std::string& make) { hint_ = make; }

    void run(std::size_t tiffAbs) {
        tiffDelta_ = tiffAbs;
        blobDelta_ = 0;
        const std::uint8_t* h = r_.ptr(tiffAbs, 8);
        if (!h) {
            res_.error = "TIFF header lies outside the file";
            return;
        }
        Endian endian;
        if (h[0] == 'I' && h[1] == 'I') endian = Endian::Little;
        else if (h[0] == 'M' && h[1] == 'M') endian = Endian::Big;
        else {
            res_.error = "not a TIFF header (expected II or MM)";
            return;
        }
        r_.reset(r_.data(), r_.size(), endian);
        res_.header.endian = endian;
        const std::uint16_t magic = (endian == Endian::Little)
            ? static_cast<std::uint16_t>(h[2] | (h[3] << 8))
            : static_cast<std::uint16_t>((h[2] << 8) | h[3]);
        res_.header.magic = magic;

        std::uint64_t firstIfd = 0;
        if (magic == 42) {
            res_.header.bigTiff = false;
            res_.header.offsetBytes = 4;
            std::uint32_t off = 0;
            if (!r_.u32(tiffAbs + 4, off)) {
                res_.error = "truncated TIFF header";
                return;
            }
            firstIfd = off;
        } else if (magic == 43) {
            res_.header.bigTiff = true;
            res_.header.offsetBytes = 8;
            std::uint16_t offSize = 0, zero = 0;
            if (!r_.u16(tiffAbs + 4, offSize) || !r_.u16(tiffAbs + 6, zero)) {
                res_.error = "truncated BigTIFF header";
                return;
            }
            if (offSize != 8) {
                res_.error = "unsupported BigTIFF offset size " + std::to_string(offSize);
                return;
            }
            // F2: the 64-bit IFD0 pointer is stored in the file's byte order.
            const std::uint8_t* p = r_.ptr(tiffAbs + 8, 8);
            if (!p) {
                res_.error = "truncated BigTIFF header";
                return;
            }
            firstIfd = readUInt64(p, endian);
        } else {
            res_.error = "unsupported TIFF magic " + std::to_string(magic);
            return;
        }
        res_.header.firstIfdOffset = firstIfd;
        res_.ok = true;

        if (firstIfd == 0) {
            warn("IFD0 pointer is zero: no image directory present");
            return;
        }

        std::uint64_t next = firstIfd;
        int chain = 0;
        IfdKind kind = IfdKind::Ifd0;
        while (next != 0 && chain < 8) {
            IfdNode node;
            if (!parseIfd(static_cast<std::size_t>(next), kind, 0, node)) break;
            if (kind != IfdKind::Ifd0) ++res_.thumbnailCount;
            const std::size_t nextStored = node.nextIfdStored;
            res_.roots.push_back(std::move(node));
            next = nextStored;
            ++chain;
            kind = IfdKind::Ifd1;
            if (!opt_.parseThumbnailIfd) break;
        }
        res_.nodeCount = nodeCount_;
        res_.tagCount = tagCount_;
    }

private:
    ByteReader r_;
    const ParseOptions& opt_;
    ParseResult& res_;
    std::string hint_;
    std::size_t tiffDelta_ = 0;
    std::size_t blobDelta_ = 0;      // buffer index of a blob's TIFF header
    std::size_t blobFileBase_ = 0;   // file offset of a blob's TIFF header
    bool inBlob_ = false;            // reading a self-contained MakerNote blob
    std::set<std::pair<const std::uint8_t*, std::size_t>> visited_;
    std::size_t nodeCount_ = 0;
    std::size_t tagCount_ = 0;

    // Saturating add/sub: a hostile 64-bit offset must never wrap a size_t
    // (unsigned overflow is defined but hides the failure from sanitizers).
    static std::size_t addClamped(std::size_t a, std::size_t b) {
        const std::size_t max = std::numeric_limits<std::size_t>::max();
        return b > max - a ? max : a + b;
    }
    static std::size_t subClamped(std::size_t a, std::size_t b) { return a < b ? 0 : a - b; }

    std::size_t absOf(std::uint64_t stored) const {
        const std::size_t s = static_cast<std::size_t>(stored);
        if (inBlob_) return addClamped(blobFileBase_, s);
        return addClamped(tiffDelta_, s);
    }

    // Buffer index the current directory's stored offsets are relative to.
    std::size_t storedBase() const { return inBlob_ ? blobDelta_ : tiffDelta_; }

    std::size_t readAt(std::uint64_t stored) const {
        const std::size_t s = static_cast<std::size_t>(stored);
        if (inBlob_) return addClamped(blobDelta_, s);
        return addClamped(tiffDelta_, s);
    }

    // File offset of a position in the buffer currently being parsed.
    std::size_t fileOf(std::size_t bufferIndex) const {
        if (inBlob_) return addClamped(blobFileBase_, subClamped(bufferIndex, blobDelta_));
        return bufferIndex;   // the whole file is the buffer at top level
    }

    // Clamp the user/library depth limit to a value the stack can survive.
    int depthLimit() const {
        if (opt_.maxDepth < 0) return 0;
        return opt_.maxDepth > kMaxIfdDepth ? kMaxIfdDepth : opt_.maxDepth;
    }

    void warn(const std::string& msg) {
        if (res_.warnings.size() < 64) res_.warnings.push_back(msg);
    }

    // Read an unsigned integer of `width` bytes in the container byte order.
    bool readWidth(std::size_t at, int width, std::uint64_t& out) const {
        if (width == 8) {
            // F2: an 8-byte offset is a plain 64-bit integer in the file's
            // byte order, not a "hi word first" pair.
            const std::uint8_t* p = r_.ptr(at, 8);
            if (!p) return false;
            out = readUInt64(p, res_.header.endian);
            return true;
        }
        std::uint32_t v = 0;
        if (!r_.u32(at, v)) return false;
        out = v;
        return true;
    }

    // Directory counts, next-IFD links and out-of-line payload pointers all
    // use the container's offset width.
    bool readOffset(std::size_t at, std::uint64_t& out) const {
        return readWidth(at, res_.header.offsetBytes, out);
    }

    // Width of an IFD-offset value inside a tag payload: LONG (4) and IFD (13)
    // stay 4 bytes even in BigTIFF; LONG8 (16), SLONG8 (17) and IFD8 (18) are 8.
    static int pointerWidth(std::uint16_t type) {
        return (type == 16 || type == 17 || type == 18) ? 8 : 4;
    }

    // blobFileBase is the file offset of the block's own TIFF header.
    // innerBigTiff must describe the blob's own header: a classic directory
    // embedded in a BigTIFF file still uses 2-byte counts and 12-byte entries.
    bool parseEmbedded(const std::vector<std::uint8_t>& blob, std::size_t innerTiffLocal,
                       std::size_t blobFileBase, std::size_t firstIfdStored, Endian inner,
                       bool innerBigTiff, int depth, IfdNode& out) {
        const ByteReader savedReader = r_;
        const std::size_t savedTiff = tiffDelta_;
        const std::size_t savedBlob = blobDelta_;
        const std::size_t savedBlobFile = blobFileBase_;
        const bool savedInBlob = inBlob_;
        const Endian savedEndian = res_.header.endian;
        const bool savedBig = res_.header.bigTiff;
        const int savedOffsetBytes = res_.header.offsetBytes;
        std::set<std::pair<const std::uint8_t*, std::size_t>> savedVisited = visited_;
        visited_.clear();

        r_.reset(blob.data(), blob.size(), inner);
        tiffDelta_ = 0;
        blobDelta_ = innerTiffLocal;
        blobFileBase_ = blobFileBase;
        inBlob_ = true;
        res_.header.endian = inner;
        res_.header.bigTiff = innerBigTiff;
        res_.header.offsetBytes = innerBigTiff ? 8 : 4;
        const bool ok = parseIfd(firstIfdStored, IfdKind::MakerNote, depth, out);

        r_ = savedReader;
        tiffDelta_ = savedTiff;
        blobDelta_ = savedBlob;
        blobFileBase_ = savedBlobFile;
        inBlob_ = savedInBlob;
        res_.header.endian = savedEndian;
        res_.header.bigTiff = savedBig;
        res_.header.offsetBytes = savedOffsetBytes;
        visited_ = std::move(savedVisited);
        return ok;
    }

    bool parseIfd(std::size_t storedOffset, IfdKind kind, int depth, IfdNode& node) {
        // F3: an all-ones 64-bit offset (or any offset past the file) must be
        // rejected before it is added to a base and wraps around.
        if (storedOffset > r_.size()) {
            warn("IFD offset 0x" + hexOf(storedOffset) + " is outside the file");
            return false;
        }
        const std::size_t dirAt = readAt(storedOffset);
        node.offset = absOf(storedOffset);
        node.storeOffset = storedOffset;
        node.kind = kind;
        node.name = ifdKindName(kind);
        node.endianness = (res_.header.endian == Endian::Little ? "little" : "big");
        node.maker = hint_;

        if (depth > depthLimit()) {
            warn("maximum IFD depth (" + std::to_string(depthLimit()) + ") reached at 0x" + hexOf(storedOffset));
            return false;
        }
        if (nodeCount_ >= opt_.maxNodes) {
            warn("node budget exhausted (" + std::to_string(opt_.maxNodes) + " directories)");
            return false;
        }
        if (!visited_.insert({r_.data(), dirAt}).second) {
            warn("IFD at 0x" + hexOf(storedOffset) + " already visited (cycle or shared pointer)");
            return false;
        }
        ++nodeCount_;

        std::uint64_t count = 0;
        if (res_.header.bigTiff) {
            if (!readOffset(dirAt, count)) {
                warn("directory count at 0x" + hexOf(storedOffset) + " is outside the file");
                return false;
            }
        } else {
            std::uint16_t c = 0;
            if (!r_.u16(dirAt, c)) {
                warn("directory count at 0x" + hexOf(storedOffset) + " is outside the file");
                return false;
            }
            count = c;
        }
        node.entryCount = count;

        // Classic entry: tag2 type2 count4 value4 (12 bytes, value at e+8).
        // BigTIFF entry: tag2 type2 count8 value8 (20 bytes, value at e+12).
        const std::size_t entrySize = res_.header.bigTiff ? 20 : 12;
        const std::size_t firstEntry = dirAt + (res_.header.bigTiff ? 8 : 2);
        const std::size_t valueStore = res_.header.bigTiff ? 8 : 4;
        const std::size_t valueField = res_.header.bigTiff ? 12 : 8;

        if (count > 0) {
            // F3: compare by division. `count * entrySize` wraps around for a
            // hostile 64-bit BigTIFF count and can land back inside the file.
            if (firstEntry > r_.size() || count > (r_.size() - firstEntry) / entrySize) {
                const std::size_t room =
                    r_.size() > firstEntry ? (r_.size() - firstEntry) / entrySize : 0;
                warn("directory at 0x" + hexOf(storedOffset) + " claims " + std::to_string(count) +
                     " entries but only " + std::to_string(room) + " fit in the file");
                return false;
            }
            // Hard ceiling: even a directory that fits must not balloon.
            if (count > kMaxEntriesPerIfd) {
                warn("directory at 0x" + hexOf(storedOffset) + " claims " + std::to_string(count) +
                     " entries, above the " + std::to_string(kMaxEntriesPerIfd) + " entry limit");
                return false;
            }
        }

        for (std::uint64_t i = 0; i < count; ++i) {
            const std::size_t e = firstEntry + static_cast<std::size_t>(i) * entrySize;
            TagEntry tag;
            std::uint16_t id = 0, type = 0;
            std::uint64_t n = 0;
            if (!r_.u16(e, id) || !r_.u16(e + 2, type)) continue;
            if (!readOffset(e + 4, n)) continue;
            tag.tag = id;
            tag.type = type;
            tag.count = n;
            tag.typeName = typeName(type);

            const int ts = typeSize(type);
            // F3: n * ts overflows uint64 for hostile BigTIFF counts.
            std::uint64_t total = 0;
            if (ts > 0) {
                const std::uint64_t tsu = static_cast<std::uint64_t>(ts);
                total = (n > std::numeric_limits<std::uint64_t>::max() / tsu)
                            ? std::numeric_limits<std::uint64_t>::max()
                            : n * tsu;
            }
            tag.valueSize = static_cast<std::size_t>(std::min<std::uint64_t>(total, 0x7FFFFFFF));

            if (ts > 0 && total > 0) {
                std::size_t readFrom = 0;
                if (total <= valueStore) {
                    tag.inlineValue = true;
                    tag.valueOffsetStored = subClamped(e + valueField, storedBase());
                    tag.valueOffsetAbs = fileOf(e + valueField);
                    readFrom = e + valueField;
                } else {
                    std::uint64_t stored = 0;
                    if (!readOffset(e + valueField, stored)) {
                        tag.valueResolved = false;
                        tag.status = "unresolved";
                    } else if (stored > r_.size()) {
                        // F3: the stored payload offset cannot address the buffer.
                        tag.valueResolved = false;
                        tag.status = "unresolved (payload offset outside file)";
                    } else {
                        tag.valueOffsetStored = static_cast<std::size_t>(stored);
                        tag.valueOffsetAbs = absOf(stored);
                        readFrom = readAt(stored);
                    }
                }
                if (tag.valueResolved) {
                    const std::uint8_t* p = r_.ptr(readFrom, tag.valueSize);
                    if (p) {
                        tag.raw.assign(p, p + tag.valueSize);
                    } else {
                        tag.valueResolved = false;
                        tag.status = "unresolved (payload outside file)";
                    }
                }
            } else if (ts == 0) {
                tag.valueResolved = false;
                tag.status = "unknown field type " + std::to_string(type);
            }

            const std::string& dictMaker = !node.maker.empty() ? node.maker : hint_;
            const TagInfo* info = lookupTag(kind, dictMaker.c_str(), id);
            tag.tagName = info ? info->name : "";

            if (kind != IfdKind::MakerNote) {
                if (id == 0x8769) { tag.isPointer = true; tag.subIfdName = "ExifIFD"; tag.pointerKind_ = IfdKind::Exif; }
                else if (id == 0x8825) { tag.isPointer = true; tag.subIfdName = "GPSIFD"; tag.pointerKind_ = IfdKind::Gps; }
                else if (id == 0xA005) { tag.isPointer = true; tag.subIfdName = "InteropIFD"; tag.pointerKind_ = IfdKind::Interop; }
                else if (id == 0x014A) { tag.isPointer = true; tag.subIfdName = "SubIFD"; tag.isSubIfdList_ = true; }
                else if (id == 0x0201) { tag.isThumbnailPointer = true; }
            }
            if (kind == IfdKind::Exif && id == 0x927C) tag.isMakerNote = true;

            formatEntry(tag, res_.header.endian, kind);
            ++tagCount_;
            node.tags.push_back(std::move(tag));
        }

        std::uint64_t nextOff = 0;
        const std::size_t nextAt = firstEntry + static_cast<std::size_t>(count) * entrySize;
        if (readOffset(nextAt, nextOff)) node.nextIfdStored = static_cast<std::size_t>(nextOff);

        if (depth < depthLimit()) {
            const std::vector<TagEntry> copy = node.tags;
            for (const TagEntry& tag : copy) {
                if (tag.isMakerNote) {
                    if (opt_.parseMakerNote) parseMakerNote(node, tag, depth + 1);
                    continue;
                }
                if (!tag.isPointer || !tag.valueResolved) continue;
                if (tag.isSubIfdList_) {
                    // F2: each SubIFD entry is an offset whose width follows
                    // the tag type (LONG = 4 bytes even in BigTIFF).
                    const int w = pointerWidth(tag.type);
                    const std::size_t step = static_cast<std::size_t>(w);
                    for (std::size_t k = 0; k < tag.count && k < 64; ++k) {
                        std::uint64_t target = 0;
                        if (!readWidth(readAt(tag.valueOffsetStored) + step * k, w, target)) break;
                        if (target == 0) continue;
                        IfdNode child;
                        if (parseIfd(static_cast<std::size_t>(target), IfdKind::Unknown, depth + 1, child)) {
                            child.name = "SubIFD[" + std::to_string(k) + "]";
                            classifyIfd(child);
                            node.children.push_back(std::move(child));
                        }
                    }
                    continue;
                }
                std::uint64_t target = 0;
                if (!readWidth(readAt(tag.valueOffsetStored), pointerWidth(tag.type), target)) continue;
                if (target == 0) continue;
                IfdNode child;
                if (parseIfd(static_cast<std::size_t>(target), tag.pointerKind_, depth + 1, child)) {
                    child.name = tag.subIfdName;
                    classifyIfd(child);
                    node.children.push_back(std::move(child));
                }
            }
        }
        return true;
    }

    void parseMakerNote(IfdNode& parent, const TagEntry& tag, int depth) {
        if (!tag.valueResolved || tag.raw.empty()) {
            warn("MakerNote payload is not readable; only its size is known");
            return;
        }
        const std::string maker = detectMaker(hint_, tag.raw.data(), tag.raw.size());
        if (!maker.empty() && res_.detectedMaker.empty()) res_.detectedMaker = maker;

        IfdNode child;
        child.kind = IfdKind::MakerNote;
        child.maker = maker;
        child.name = maker.empty() ? "MakerNote" : ("MakerNote (" + maker + ")");
        child.offset = tag.valueOffsetAbs;
        child.storeOffset = tag.valueOffsetStored;
        child.endianness = (res_.header.endian == Endian::Little ? "little" : "big");

        const std::uint8_t* d = tag.raw.data();
        const std::size_t n = tag.raw.size();

        if (maker == "Nikon" && n > 18 && d[6] == 0x02) {
            const std::size_t kHeader = 10;
            const std::uint8_t* h = d + kHeader;
            if ((h[0] == 'I' && h[1] == 'I') || (h[0] == 'M' && h[1] == 'M')) {
                const Endian inner = (h[0] == 'I') ? Endian::Little : Endian::Big;
                const std::uint16_t innerMagic = static_cast<std::uint16_t>(
                    inner == Endian::Little ? (h[2] | (h[3] << 8)) : ((h[2] << 8) | h[3]));
                const bool innerBig = (innerMagic == 43);
                std::uint64_t off = 0;
                if (innerBig) {
                    off = readUInt64(h + 8, inner);
                } else if (innerMagic == 42) {
                    off = readUInt(h + 4, 4, inner);
                }
                const std::vector<std::uint8_t> blob(tag.raw.begin(), tag.raw.end());
                IfdNode mn;
                const std::size_t headerFile = tag.valueOffsetAbs + kHeader;
                if (parseEmbedded(blob, kHeader, headerFile, static_cast<std::size_t>(off), inner,
                                  innerBig, depth, mn) && !mn.tags.empty()) {
                    mn.name = "MakerNote (Nikon type 3)";
                    mn.maker = maker;
                    mn.endianness = (inner == Endian::Little ? "little" : "big");
                    classifyIfd(mn);
                    child.children.push_back(std::move(mn));
                    parent.children.push_back(std::move(child));
                    return;
                }
            }
        }

        if (maker == "Olympus" && n >= 12 && std::memcmp(d, "OLYMPUS\0II", 10) == 0) {
            const std::uint32_t dir = readUInt(d + 8, 4, res_.header.endian);
            if (dir != 0) {
                IfdNode mn;
                // The vendor directory is always classic 12-byte format.
                const bool savedBig = res_.header.bigTiff;
                const int savedOffBytes = res_.header.offsetBytes;
                res_.header.bigTiff = false;
                res_.header.offsetBytes = 4;
                const bool ok = parseIfd(dir, IfdKind::MakerNote, depth, mn);
                res_.header.bigTiff = savedBig;
                res_.header.offsetBytes = savedOffBytes;
                if (ok && !mn.tags.empty()) {
                    mn.name = "MakerNote (Olympus)";
                    mn.maker = maker;
                    child.children.push_back(std::move(mn));
                    parent.children.push_back(std::move(child));
                    return;
                }
            }
        }

        if (maker == "Fujifilm" && n >= 12) {
            const std::uint32_t dir = readUInt(d + 8, 4, res_.header.endian);
            if (dir != 0) {
                IfdNode mn;
                // The vendor directory is always classic 12-byte format.
                const bool savedBig = res_.header.bigTiff;
                const int savedOffBytes = res_.header.offsetBytes;
                res_.header.bigTiff = false;
                res_.header.offsetBytes = 4;
                const bool ok = parseIfd(dir, IfdKind::MakerNote, depth, mn);
                res_.header.bigTiff = savedBig;
                res_.header.offsetBytes = savedOffBytes;
                if (ok && !mn.tags.empty()) {
                    mn.name = "MakerNote (Fujifilm)";
                    mn.maker = maker;
                    child.children.push_back(std::move(mn));
                    parent.children.push_back(std::move(child));
                    return;
                }
            }
        }

        const std::size_t candidates[] = {0, 12, 10, 8, 6};
        for (std::size_t skip : candidates) {
            if (skip + 2 > n) continue;
            std::uint16_t first = 0;
            if (res_.header.endian == Endian::Little) {
                first = static_cast<std::uint16_t>(d[skip] | (d[skip + 1] << 8));
            } else {
                first = static_cast<std::uint16_t>((d[skip] << 8) | d[skip + 1]);
            }
            if (first == 0 || first > 1024) continue;
            const std::vector<std::uint8_t> blob(tag.raw.begin(), tag.raw.end());
            IfdNode mn;
            const std::size_t blobFile = tag.valueOffsetAbs + skip;
            if (!parseEmbedded(blob, skip, blobFile, 0, res_.header.endian, false, depth, mn)) continue;
            if (mn.tags.empty()) continue;
            mn.name = maker.empty() ? "MakerNote IFD" : ("MakerNote (" + maker + ")");
            mn.maker = maker;
            classifyIfd(mn);
            child.children.push_back(std::move(mn));
            parent.children.push_back(std::move(child));
            return;
        }

        TagEntry opaque;
        opaque.tag = 0;
        opaque.type = 7;
        opaque.typeName = "UNDEFINED";
        opaque.count = tag.count;
        opaque.valueSize = tag.valueSize;
        opaque.valueOffsetAbs = tag.valueOffsetAbs;
        opaque.valueOffsetStored = tag.valueOffsetStored;
        opaque.raw = tag.raw;
        opaque.tagName = "OpaqueMakerNote";
        opaque.value = "opaque " + std::to_string(tag.valueSize) + "-byte block at 0x" +
                       hexOf(tag.valueOffsetStored) + ", preview [" + hexBytes(tag.raw, 16) + "]";
        opaque.json = "\"opaque\"";
        child.tags.push_back(std::move(opaque));
        parent.children.push_back(std::move(child));
    }

    void classifyIfd(IfdNode& node) {
        if (node.kind != IfdKind::Unknown) return;
        if (node.name.find("Exif") != std::string::npos) node.kind = IfdKind::Exif;
        else if (node.name.find("GPS") != std::string::npos) node.kind = IfdKind::Gps;
        else if (node.name.find("Interop") != std::string::npos) node.kind = IfdKind::Interop;
    }
};

// Best-effort Make-tag probe used only as a MakerNote dictionary hint. Handles
// classic TIFF and BigTIFF, in both byte orders (F2).
std::string probeMake(const std::uint8_t* buffer, std::size_t size, std::size_t tiffAbs) {
    if (tiffAbs + 8 > size) return "";
    const Endian endian = (buffer[tiffAbs] == 'I') ? Endian::Little : Endian::Big;
    ByteReader r(buffer, size, endian);
    std::uint16_t magic = 0;
    if (!r.u16(tiffAbs + 2, magic)) return "";
    const bool big = (magic == 43);
    if (!big && magic != 42) return "";

    std::uint64_t firstIfd = 0;
    if (big) {
        const std::uint8_t* p = r.ptr(tiffAbs + 8, 8);
        if (!p) return "";
        firstIfd = readUInt64(p, endian);
    } else {
        std::uint32_t v = 0;
        if (!r.u32(tiffAbs + 4, v)) return "";
        firstIfd = v;
    }
    if (firstIfd == 0 || firstIfd > size) return "";

    const std::size_t dir = tiffAbs + static_cast<std::size_t>(firstIfd);
    const std::size_t entrySize = big ? 20 : 12;
    const std::size_t countBytes = big ? 8 : 2;
    const std::size_t valueField = big ? 12 : 8;
    const std::size_t offsetBytes = big ? 8 : 4;

    std::uint64_t count = 0;
    if (big) {
        const std::uint8_t* p = r.ptr(dir, 8);
        if (!p) return "";
        count = readUInt64(p, endian);
    } else {
        std::uint16_t c = 0;
        if (!r.u16(dir, c)) return "";
        count = c;
    }
    const std::size_t firstEntry = dir + countBytes;
    if (firstEntry > size || count > (size - firstEntry) / entrySize) return "";

    for (std::uint64_t i = 0; i < count; ++i) {
        const std::size_t e = firstEntry + static_cast<std::size_t>(i) * entrySize;
        std::uint16_t id = 0, type = 0;
        std::uint64_t n = 0;
        if (!r.u16(e, id) || !r.u16(e + 2, type)) continue;
        const std::uint8_t* np = r.ptr(e + 4, static_cast<std::size_t>(offsetBytes));
        if (!np) continue;
        n = (offsetBytes == 8) ? readUInt64(np, endian) : readUInt(np, 4, endian);
        if (id != 0x010F || type != 2 || n == 0) continue;
        const std::size_t len = static_cast<std::size_t>(std::min<std::uint64_t>(n, 256));
        std::size_t off = 0;
        if (n <= offsetBytes) {
            off = e + valueField;
        } else {
            const std::uint8_t* sp = r.ptr(e + valueField, offsetBytes);
            if (!sp) continue;
            const std::uint64_t stored = (offsetBytes == 8) ? readUInt64(sp, endian) : readUInt(sp, 4, endian);
            if (stored > size || tiffAbs > size - static_cast<std::size_t>(stored)) continue;
            off = tiffAbs + static_cast<std::size_t>(stored);
        }
        const std::uint8_t* p = r.ptr(off, len);
        if (!p) continue;
        return trimNul(std::string(reinterpret_cast<const char*>(p), len));
    }
    return "";
}

}  // namespace

bool rawValueAt(const TagEntry& e, Endian endian, std::size_t index, std::string& out) {
    double v = 0.0;
    bool isRat = false;
    if (!valueAt(e, endian, index, v, isRat)) return false;
    char buf[96];
    if (isRat) {
        double den = 1.0;
        denominatorAt(e, endian, index, den);
        // F4: num() is NaN/Inf-safe, so a 0/0 or Inf rational cannot trip UB.
        std::snprintf(buf, sizeof(buf), "%s/%s", num(v).c_str(), num(den).c_str());
    } else if (e.type == 11 || e.type == 12) {
        if (std::isfinite(v)) std::snprintf(buf, sizeof(buf), "%.9g", v);
        else std::snprintf(buf, sizeof(buf), "%s", num(v).c_str());
    } else {
        std::snprintf(buf, sizeof(buf), "%lld", safeLL(v));   // F4: guarded cast
    }
    out = buf;
    return true;
}

double rawDoubleAt(const TagEntry& e, Endian endian, std::size_t index, bool& ok) {
    double v = 0.0;
    bool isRat = false;
    ok = valueAt(e, endian, index, v, isRat);
    if (ok && isRat) {
        double den = 1.0;
        denominatorAt(e, endian, index, den);
        v = (den != 0.0) ? v / den : 0.0;
    }
    return v;
}

ParseResult parseTiff(const std::uint8_t* buffer, std::size_t size, std::size_t tiffBaseAbs,
                      const ParseOptions& opt) {
    ParseResult res;
    res.tiffBaseAbs = tiffBaseAbs;
    if (tiffBaseAbs >= size) {
        res.error = "TIFF payload offset is outside the file";
        return res;
    }
    Parser p(buffer, size, opt, res);
    p.setMakeHint(probeMake(buffer, size, tiffBaseAbs));
    p.run(tiffBaseAbs);
    return res;
}

}  // namespace exif
