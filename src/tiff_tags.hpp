// tiff_tags.hpp - tag dictionaries for the standard IFDs and MakerNotes.
#ifndef EXIFREADER_TIFF_TAGS_HPP
#define EXIFREADER_TIFF_TAGS_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "tiff_parser.hpp"

namespace exif {

struct TagInfo {
    std::uint16_t id;
    const char* name;
    const char* comment;   // optional short hint, may be ""
};

// Returns nullptr when the tag is not in the dictionary for that IFD family.
const TagInfo* lookupTag(IfdKind kind, const char* maker, std::uint16_t id);

// Human readable list of the dictionaries compiled into the reader.
std::vector<std::string> knownTagTables();

// Raw access to one dictionary, for --list-tags.
const TagInfo* tagTableFor(IfdKind kind, const char* maker, std::size_t& count);

}  // namespace exif

#endif  // EXIFREADER_TIFF_TAGS_HPP
