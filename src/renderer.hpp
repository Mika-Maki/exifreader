// renderer.hpp - output modes for a parsed EXIF tree.
#ifndef EXIFREADER_RENDERER_HPP
#define EXIFREADER_RENDERER_HPP

#include <string>
#include <vector>

#include "container.hpp"
#include "tiff_parser.hpp"

namespace exif {

struct RenderOptions {
    bool showValues = true;
    bool showTypes = true;
    bool showRaw = false;
    bool color = false;
    bool showWarnings = true;
    std::string sourceName;   // printed in headers ("<stdin>", "./photo.jpg")
};

// Indented human readable information tree.
std::string renderTree(const ContainerInfo& container, const ParseResult& parsed, const RenderOptions& opt);

// One tag per line: path | name | value | type | count
std::string renderTags(const ContainerInfo& container, const ParseResult& parsed, const RenderOptions& opt);

// Machine readable structure with both raw and formatted values.
std::string renderJson(const ContainerInfo& container, const ParseResult& parsed, const RenderOptions& opt);

// Compact one-line summary of the most useful attributes.
std::string renderSummary(const ContainerInfo& container, const ParseResult& parsed);

// Embedded thumbnails/previews found in the IFD chain (JPEG interchange data).
struct ThumbnailRef {
    std::size_t offset = 0;   // absolute offset inside the file buffer
    std::size_t length = 0;
    std::string label;
};
std::vector<ThumbnailRef> findThumbnails(const ParseResult& parsed);

}  // namespace exif

#endif  // EXIFREADER_RENDERER_HPP
