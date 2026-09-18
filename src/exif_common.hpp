// exif_common.hpp - shared types, byte helpers, endian utilities.
//
// Part of Image EXIF Information-Tree Reader (exifreader).
#ifndef EXIFREADER_COMMON_HPP
#define EXIFREADER_COMMON_HPP

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace exif {

enum class Endian { Little, Big };

// Read an unsigned integer of 1/2/4 bytes in the given byte order.
inline std::uint32_t readUInt(const std::uint8_t* p, int bytes, Endian e) {
    std::uint32_t v = 0;
    if (e == Endian::Little) {
        for (int i = bytes - 1; i >= 0; --i) v = (v << 8) | p[i];
    } else {
        for (int i = 0; i < bytes; ++i) v = (v << 8) | p[i];
    }
    return v;
}

// Read an unsigned 64-bit integer in the given byte order.
inline std::uint64_t readUInt64(const std::uint8_t* p, Endian e) {
    std::uint64_t v = 0;
    if (e == Endian::Little) {
        for (int i = 7; i >= 0; --i) v = (v << 8) | p[i];
    } else {
        for (int i = 0; i < 8; ++i) v = (v << 8) | p[i];
    }
    return v;
}

// Read a signed integer of 1/2/4 bytes (two's complement) in the given order.
inline std::int32_t readInt(const std::uint8_t* p, int bytes, Endian e) {
    std::uint32_t u = readUInt(p, bytes, e);
    switch (bytes) {
        case 1: return static_cast<std::int8_t>(u);
        case 2: return static_cast<std::int16_t>(u);
        default: return static_cast<std::int32_t>(u);
    }
}

// Convert a little/big endian IEEE-754 value to a native double.
inline double halfToDouble(std::uint16_t h) {
    const int sign = (h >> 15) & 1;
    const int exp = (h >> 10) & 0x1F;
    const int frac = h & 0x3FF;
    double v;
    if (exp == 0) {
        v = frac / 1024.0 * (1.0 / 16384.0);   // subnormal
    } else if (exp == 31) {
        v = frac ? 0.0 : 1.0;                  // NaN/Inf -> 0/1 sentinel handled by caller
    } else {
        v = (1.0 + frac / 1024.0);
        for (int i = 0; i < exp - 15; ++i) v *= 2.0;
        for (int i = 0; i < 15 - exp; ++i) v /= 2.0;
    }
    return sign ? -v : v;
}

// Length of a well-formed UTF-8 sequence starting at s[i], or 0 when the bytes
// there are not valid UTF-8. Used to keep hostile tag payloads from producing
// invalid JSON and to sanitise terminal output.
inline int utf8SeqLen(const char* s, std::size_t n, std::size_t i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) return 1;
    int len = 0;
    unsigned char lo = 0x80, hi = 0xBF;   // bounds for the first continuation byte
    if (c >= 0xC2 && c <= 0xDF) {
        len = 2;
    } else if (c >= 0xE0 && c <= 0xEF) {
        len = 3;
        if (c == 0xE0) lo = 0xA0;         // reject overlong 3-byte forms
        if (c == 0xED) hi = 0x9F;         // reject UTF-16 surrogates
    } else if (c >= 0xF0 && c <= 0xF4) {
        len = 4;
        if (c == 0xF0) lo = 0x90;         // reject overlong 4-byte forms
        if (c == 0xF4) hi = 0x8F;         // reject code points above U+10FFFF
    } else {
        return 0;
    }
    if (i + static_cast<std::size_t>(len) > n) return 0;
    for (int k = 1; k < len; ++k) {
        const unsigned char cc = static_cast<unsigned char>(s[i + static_cast<std::size_t>(k)]);
        const unsigned char l = (k == 1) ? lo : 0x80;
        const unsigned char h = (k == 1) ? hi : 0xBF;
        if (cc < l || cc > h) return 0;
    }
    return len;
}

// Escape a tag value for terminal/tree/tags/summary output: C0 controls and DEL
// become \xNN so a hostile value cannot inject newlines or ANSI escapes.
// Valid UTF-8 is passed through; invalid bytes become '?'.
inline std::string escapeForDisplay(const std::string& s) {
    static const char* digits = "0123456789ABCDEF";
    std::string o;
    o.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x20 || c == 0x7F) {
            o += "\\x";
            o += digits[c >> 4];
            o += digits[c & 0xF];
            ++i;
        } else if (c < 0x80) {
            o += static_cast<char>(c);
            ++i;
        } else {
            const int len = utf8SeqLen(s.data(), s.size(), i);
            if (len > 0) {
                o.append(s, i, static_cast<std::size_t>(len));
                i += static_cast<std::size_t>(len);
            } else {
                o += '?';
                ++i;
            }
        }
    }
    return o;
}

// Escape a string's bytes for use inside a JSON string literal (no surrounding
// quotes). Control characters use the short escapes or \u00XX; valid UTF-8
// passes through; each invalid byte becomes U+FFFD so the result is always
// valid UTF-8 JSON.
inline std::string escapeJson(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        switch (c) {
            case '"': o += "\\\""; ++i; continue;
            case '\\': o += "\\\\"; ++i; continue;
            case '\n': o += "\\n"; ++i; continue;
            case '\r': o += "\\r"; ++i; continue;
            case '\t': o += "\\t"; ++i; continue;
            case '\b': o += "\\b"; ++i; continue;
            case '\f': o += "\\f"; ++i; continue;
            default: break;
        }
        if (c < 0x20) {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "\\u%04x", c);
            o += buf;
            ++i;
        } else if (c < 0x80) {
            o += static_cast<char>(c);
            ++i;
        } else {
            const int len = utf8SeqLen(s.data(), s.size(), i);
            if (len > 0) {
                o.append(s, i, static_cast<std::size_t>(len));
                i += static_cast<std::size_t>(len);
            } else {
                o += "\\uFFFD";
                ++i;
            }
        }
    }
    return o;
}

inline std::string toLower(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

// Trim ASCII NULs/blanks from both ends of a raw string.
inline std::string trimBytes(const std::string& s) {
    std::size_t b = 0, e = s.size();
    while (b < e && (s[b] == '\0' || s[b] == ' ' || s[b] == '\r' || s[b] == '\n' || s[b] == '\t')) ++b;
    while (e > b && (s[e - 1] == '\0' || s[e - 1] == ' ' || s[e - 1] == '\r' || s[e - 1] == '\n' || s[e - 1] == '\t')) --e;
    return s.substr(b, e - b);
}

}  // namespace exif

#endif  // EXIFREADER_COMMON_HPP
