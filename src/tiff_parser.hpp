// tiff_parser.hpp - EXIF/TIFF IFD parser producing an addressable tree.
#ifndef EXIFREADER_TIFF_PARSER_HPP
#define EXIFREADER_TIFF_PARSER_HPP

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "byte_reader.hpp"
#include "exif_common.hpp"

namespace exif {

// The IFD family a tag id belongs to (selects the tag dictionary).
enum class IfdKind { Ifd0, Ifd1, Exif, Gps, Interop, MakerNote, Unknown };

const char* ifdKindName(IfdKind k);

struct TiffHeaderInfo {
    Endian endian = Endian::Little;
    std::uint16_t magic = 42;
    std::uint64_t firstIfdOffset = 0;
    bool bigTiff = false;
    int offsetBytes = 4;
};

struct TagEntry {
    std::uint16_t tag = 0;
    std::uint16_t type = 0;
    std::uint64_t count = 0;
    std::string typeName;

    bool inlineValue = false;          // value stored in the 4/8-byte entry field
    std::size_t valueOffsetStored = 0; // offset as stored in the file
    std::size_t valueOffsetAbs = 0;    // absolute offset inside the buffer
    std::size_t valueSize = 0;         // byte length of the value payload
    bool valueResolved = true;         // false when the payload lies outside the buffer

    std::vector<std::uint8_t> raw;     // payload bytes (inline or copied out)
    std::string value;                 // formatted, human readable value
    std::string json;                  // formatted value as a JSON fragment

    // Name of the pointed-to IFD when this tag is a known sub-IFD pointer.
    std::string subIfdName;
    bool isPointer = false;
    bool isMakerNote = false;
    bool isThumbnailPointer = false;

    std::string tagName;               // resolved dictionary name ("" when unknown)
    std::string status;                // non-empty on anomalies ("unresolved", ...)

    // Parser bookkeeping for sub-IFD pointers (not rendered directly).
    IfdKind pointerKind_ = IfdKind::Unknown;
    bool isSubIfdList_ = false;
};

struct IfdNode {
    std::string name;                  // "IFD0", "ExifIFD", "MakerNote PENTAX", ...
    IfdKind kind = IfdKind::Unknown;
    std::size_t offset = 0;            // absolute offset of the directory
    std::size_t storeOffset = 0;       // offset relative to the TIFF header
    std::string endianness;            // "little" / "big"
    std::string maker;                 // maker-note vendor when detected
    std::uint64_t entryCount = 0;
    std::size_t nextIfdStored = 0;
    std::vector<TagEntry> tags;
    std::vector<IfdNode> children;
};

// Hard ceiling applied to ParseOptions::maxDepth. A deeply nested SubIFD chain
// with an inflated limit overflows the stack, so the parser clamps to this (F6).
constexpr int kMaxIfdDepth = 256;

struct ParseOptions {
    bool parseMakerNote = true;
    bool parseThumbnailIfd = true;
    int maxDepth = 8;
    std::size_t maxNodes = 200000;
};

struct ParseResult {
    bool ok = false;
    std::string error;
    TiffHeaderInfo header;
    std::size_t tiffBaseAbs = 0;       // absolute offset of the TIFF header
    std::vector<IfdNode> roots;        // IFD0 followed by its IFD1 sibling chain
    std::vector<std::string> warnings;
    std::size_t nodeCount = 0;
    std::size_t tagCount = 0;
    std::size_t thumbnailCount = 0;   // IFD1 directories in the chain
    std::string detectedMaker;
};

// Parse a TIFF/EXIF payload. The buffer must outlive the result: no pixel or
// payload data is copied beyond the (small) tag value fields.
ParseResult parseTiff(const std::uint8_t* buffer, std::size_t size,
                      std::size_t tiffBaseAbs, const ParseOptions& opt);

// Type helpers shared with the renderers.
int typeSize(std::uint16_t type);
const char* typeName(std::uint16_t type);
// Extract the n-th value of a decoded entry as a string ("num/den" for rationals).
bool rawValueAt(const TagEntry& e, Endian endian, std::size_t index, std::string& out);
double rawDoubleAt(const TagEntry& e, Endian endian, std::size_t index, bool& ok);
bool isAsciiType(std::uint16_t type);
bool isRationalType(std::uint16_t type);
std::size_t valueCount(const TagEntry& e);

}  // namespace exif

#endif  // EXIFREADER_TIFF_PARSER_HPP
