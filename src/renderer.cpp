// renderer.cpp - tree / tags / JSON outputs.
#include "renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <sstream>

namespace exif {

namespace {

std::string hexOf(std::size_t v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "0x%zX", v);
    return buf;
}

std::string typeAndCount(const TagEntry& e) {
    return std::string(e.typeName) + "[" + std::to_string(e.count) + "]";
}

std::string tagLabel(const TagEntry& e) {
    std::string name = e.tagName.empty() ? "Unknown" : e.tagName;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%04X", e.tag);
    return name + " (" + buf + ")";
}

std::string ifdHeadline(const IfdNode& n) {
    std::string s = n.name;
    s += "  @ TIFF+" + hexOf(n.storeOffset) + " (file " + hexOf(n.offset) + ")";
    s += "  " + std::to_string(n.entryCount) + " entries, ";
    s += (n.endianness == "little" ? "little" : "big");
    s += "-endian";
    if (!n.maker.empty() && n.kind == IfdKind::MakerNote) s += ", maker " + n.maker;
    if (n.nextIfdStored != 0) s += ", next IFD at TIFF+" + hexOf(n.nextIfdStored);
    return s;
}

void renderNode(const IfdNode& node, std::size_t depth, const RenderOptions& opt, std::ostream& os,
                int& linesLeft) {
    if (linesLeft <= 0) return;
    const std::string indent(depth * 2, ' ');
    os << indent << (depth == 0 ? "" : "|-- ") << ifdHeadline(node) << "\n";
    --linesLeft;

    for (const TagEntry& t : node.tags) {
        if (linesLeft <= 0) return;
        os << indent << "    " << tagLabel(t);
        if (opt.showTypes) {
            os << "  : " << typeAndCount(t) << (t.inlineValue ? " inline" : "");
        }
        if (opt.showValues) os << " = " << t.value;
        if (!t.status.empty()) os << "  [!] " << t.status;
        if (opt.showRaw && !t.raw.empty()) {
            os << "  raw=";
            const std::size_t n = std::min<std::size_t>(t.raw.size(), 32);
            for (std::size_t i = 0; i < n; ++i) {
                char b[4];
                std::snprintf(b, sizeof(b), "%02x", t.raw[i]);
                os << b;
            }
            if (t.raw.size() > n) os << "..";
        }
        os << "\n";
        --linesLeft;
    }

    for (const IfdNode& child : node.children) renderNode(child, depth + 1, opt, os, linesLeft);
}

std::string findScalar(const ParseResult& p, std::uint16_t tag, IfdKind kind) {
    for (const IfdNode& root : p.roots) {
        std::vector<const IfdNode*> stack{&root};
        while (!stack.empty()) {
            const IfdNode* n = stack.back();
            stack.pop_back();
            if (n->kind == kind || kind == IfdKind::Unknown) {
                for (const TagEntry& t : n->tags) {
                    if (t.tag == tag && !t.value.empty()) return t.value;
                }
            }
            for (const IfdNode& c : n->children) stack.push_back(&c);
        }
    }
    return "";
}

void collectTagLines(const IfdNode& node, const std::string& prefix, const RenderOptions& opt,
                     std::vector<std::string>& out) {
    const std::string path = prefix.empty() ? node.name : prefix + "/" + node.name;
    for (const TagEntry& t : node.tags) {
        const std::string name = t.tagName.empty() ? "Unknown" : t.tagName;
        char idbuf[16];
        std::snprintf(idbuf, sizeof(idbuf), "0x%04X", t.tag);
        std::string line = path + " | " + name + " (" + idbuf + ") | ";
        if (opt.showValues) line += t.value;
        line += " | ";
        if (opt.showTypes) line += typeAndCount(t);
        if (!t.status.empty()) line += " | " + t.status;
        out.push_back(line);
        if (out.size() > 500000) return;
    }
    for (const IfdNode& c : node.children) collectTagLines(c, path, opt, out);
}

}  // namespace

std::string renderTree(const ContainerInfo& container, const ParseResult& parsed, const RenderOptions& opt) {
    std::ostringstream os;
    const std::string name = opt.sourceName.empty() ? "<input>" : opt.sourceName;
    os << name << "\n";
    os << " +- container: " << containerName(container.kind);
    // F7: formatDetail/frameType/note can carry raw bytes from the file (for
    // example an ISO-BMFF major brand), so they are escaped like tag values.
    if (!container.formatDetail.empty()) os << " - " << escapeForDisplay(container.formatDetail);
    if (container.width || container.height) os << "  " << container.width << "x" << container.height;
    if (!container.frameType.empty()) os << "  [" << escapeForDisplay(container.frameType) << "]";
    os << "\n";

    for (const EmbeddedExif& ee : container.exifCandidates) {
        if (ee.found) os << " |   +- EXIF payload: " << escapeForDisplay(ee.note) << "\n";
    }

    if (!parsed.ok) {
        os << " +- no EXIF data";
        if (!parsed.error.empty()) os << " (" << parsed.error << ")";
        os << "\n";
        return os.str();
    }

    os << " +- TIFF header @ " << hexOf(parsed.tiffBaseAbs) << ": "
       << (parsed.header.endian == Endian::Little ? "II (little-endian)" : "MM (big-endian)")
       << ", magic " << parsed.header.magic << (parsed.header.bigTiff ? ", BigTIFF" : "")
       << ", IFD0 at TIFF+" << hexOf(static_cast<std::size_t>(parsed.header.firstIfdOffset))
       << " (file " << hexOf(parsed.tiffBaseAbs + static_cast<std::size_t>(parsed.header.firstIfdOffset))
       << ")\n";

    int linesLeft = 200000;
    for (const IfdNode& root : parsed.roots) renderNode(root, 1, opt, os, linesLeft);

    if (opt.showWarnings && !parsed.warnings.empty()) {
        os << " ! " << parsed.warnings.size() << " warning(s):\n";
        for (const std::string& w : parsed.warnings) os << "   - " << w << "\n";
    }
    os << " = " << parsed.nodeCount << " IFD(s)";
    if (parsed.thumbnailCount) os << " (incl. " << parsed.thumbnailCount << " thumbnail)";
    os << ", " << parsed.tagCount << " tag(s)\n";
    return os.str();
}

std::string renderTags(const ContainerInfo& container, const ParseResult& parsed, const RenderOptions& opt) {
    std::ostringstream os;
    (void)container;
    if (!parsed.ok) {
        os << (opt.sourceName.empty() ? "<input>" : opt.sourceName) << " | ERROR | "
           << (parsed.error.empty() ? "no EXIF data" : parsed.error) << "\n";
        return os.str();
    }
    std::vector<std::string> lines;
    for (const IfdNode& root : parsed.roots) collectTagLines(root, "", opt, lines);
    for (const std::string& l : lines) os << l << "\n";
    if (opt.showWarnings) {
        for (const std::string& w : parsed.warnings) os << "# warning: " << w << "\n";
    }
    return os.str();
}

namespace {

// F7: escapeJson() (exif_common.hpp) emits control characters as escapes and
// keeps the result valid UTF-8, replacing malformed bytes with U+FFFD.
void jsonString(std::ostream& os, const std::string& s) {
    os << '"' << escapeJson(s) << '"';
}

void jsonNode(const IfdNode& n, const RenderOptions& opt, std::ostream& os, const std::string& indent) {
    const std::string inner = indent + "  ";
    os << "{\n";
    os << inner << "\"ifd\": "; jsonString(os, n.name); os << ",\n";
    os << inner << "\"kind\": "; jsonString(os, ifdKindName(n.kind)); os << ",\n";
    os << inner << "\"offset\": \"TIFF+" << hexOf(n.storeOffset) << "\",\n";
    os << inner << "\"fileOffset\": \"" << hexOf(n.offset) << "\",\n";
    os << inner << "\"endianness\": "; jsonString(os, n.endianness); os << ",\n";
    if (!n.maker.empty()) { os << inner << "\"maker\": "; jsonString(os, n.maker); os << ",\n"; }
    os << inner << "\"entryCount\": " << n.entryCount << ",\n";
    if (n.nextIfdStored) os << inner << "\"nextIfd\": \"TIFF+" << hexOf(n.nextIfdStored) << "\",\n";
    os << inner << "\"tags\": [";
    if (!n.tags.empty()) os << "\n";
    for (std::size_t i = 0; i < n.tags.size(); ++i) {
        const TagEntry& t = n.tags[i];
        const std::string ti = inner + "  ";
        char idbuf[16];
        std::snprintf(idbuf, sizeof(idbuf), "0x%04X", t.tag);
        os << ti << "{\"id\": \"" << idbuf << "\", \"name\": ";
        jsonString(os, t.tagName.empty() ? "Unknown" : t.tagName);
        os << ", \"type\": "; jsonString(os, t.typeName);
        os << ", \"count\": " << t.count;
        os << ", \"offset\": \"" << hexOf(t.valueOffsetStored) << "\"";
        os << ", \"fileOffset\": \"" << hexOf(t.valueOffsetAbs) << "\"";
        os << ", \"inline\": " << (t.inlineValue ? "true" : "false");
        if (opt.showValues) { os << ", \"value\": " << (t.json.empty() ? "\"\"" : t.json); }
        if (!t.status.empty()) { os << ", \"status\": "; jsonString(os, t.status); }
        if (t.isPointer) { os << ", \"pointsToIfd\": "; jsonString(os, t.subIfdName); }
        if (t.isMakerNote) os << ", \"makerNote\": true";
        if (t.isThumbnailPointer) os << ", \"thumbnailPointer\": true";
        if (opt.showRaw && !t.raw.empty()) {
            os << ", \"raw\": \"";
            const std::size_t nn = std::min<std::size_t>(t.raw.size(), 64);
            for (std::size_t k = 0; k < nn; ++k) {
                char b[4];
                std::snprintf(b, sizeof(b), "%02x", t.raw[k]);
                os << b;
            }
            if (t.raw.size() > nn) os << "..";
            os << "\"";
        }
        os << "}";
        if (i + 1 < n.tags.size()) os << ",";
        os << "\n";
    }
    if (!n.tags.empty()) os << inner;
    os << "],\n";
    os << inner << "\"children\": [";
    if (!n.children.empty()) os << "\n";
    for (std::size_t i = 0; i < n.children.size(); ++i) {
        os << inner << "  ";
        jsonNode(n.children[i], opt, os, inner + "  ");
        if (i + 1 < n.children.size()) os << ",";
        os << "\n";
    }
    if (!n.children.empty()) os << inner;
    os << "]\n";
    os << indent << "}";
}

}  // namespace

std::string renderJson(const ContainerInfo& container, const ParseResult& parsed, const RenderOptions& opt) {
    std::ostringstream os;
    os << "{\n  \"source\": ";
    jsonString(os, opt.sourceName);
    os << ",\n  \"container\": {\n    \"format\": ";
    jsonString(os, containerName(container.kind));
    os << ",\n    \"detail\": ";
    jsonString(os, container.formatDetail);
    os << ",\n    \"width\": " << container.width << ",\n    \"height\": " << container.height
       << ",\n    \"exifPayloads\": [";
    for (std::size_t i = 0; i < container.exifCandidates.size(); ++i) {
        const EmbeddedExif& ee = container.exifCandidates[i];
        os << (i ? ", " : "") << "{\"offset\": \"" << hexOf(ee.payloadOffset) << "\", \"note\": ";
        jsonString(os, ee.note);
        os << "}";
    }
    os << "]\n  },\n  \"exif\": {\n    \"found\": " << (parsed.ok ? "true" : "false") << ",\n";
    if (!parsed.ok) {
        os << "    \"error\": "; jsonString(os, parsed.error); os << "\n  }\n}\n";
        return os.str();
    }
    os << "    \"tiffOffset\": \"" << hexOf(parsed.tiffBaseAbs) << "\",\n";
    os << "    \"endianness\": " << (parsed.header.endian == Endian::Little ? "\"little\"" : "\"big\"") << ",\n";
    os << "    \"bigTiff\": " << (parsed.header.bigTiff ? "true" : "false") << ",\n";
    os << "    \"ifdCount\": " << parsed.nodeCount << ",\n";
    os << "    \"tagCount\": " << parsed.tagCount << ",\n";
    if (!parsed.detectedMaker.empty()) {
        os << "    \"makerNoteVendor\": "; jsonString(os, parsed.detectedMaker); os << ",\n";
    }
    os << "    \"warnings\": [";
    for (std::size_t i = 0; i < parsed.warnings.size(); ++i) {
        if (i) os << ", ";
        jsonString(os, parsed.warnings[i]);
    }
    os << "],\n    \"ifds\": [\n";
    std::vector<const IfdNode*> all;
    for (const IfdNode& r : parsed.roots) all.push_back(&r);
    for (std::size_t i = 0; i < all.size(); ++i) {
        os << "      ";
        jsonNode(*all[i], opt, os, "      ");
        if (i + 1 < all.size()) os << ",";
        os << "\n";
    }
    os << "    ]\n  }\n}\n";
    return os.str();
}

std::string renderSummary(const ContainerInfo& container, const ParseResult& parsed) {
    std::ostringstream os;
    os << containerName(container.kind);
    if (container.width || container.height) os << " " << container.width << "x" << container.height;
    if (!parsed.ok) {
        os << ", no EXIF";
        if (!parsed.error.empty()) os << " (" << parsed.error << ")";
        return os.str();
    }
    os << (parsed.header.endian == Endian::Little ? ", little" : ", big") << "-endian";
    os << ", " << parsed.nodeCount << " IFD(s), " << parsed.tagCount << " tag(s)";
    const std::string make = findScalar(parsed, 0x010F, IfdKind::Ifd0);
    const std::string model = findScalar(parsed, 0x0110, IfdKind::Ifd0);
    const std::string date = findScalar(parsed, 0x9003, IfdKind::Exif);
    const std::string exposure = findScalar(parsed, 0x829A, IfdKind::Exif);
    const std::string fnum = findScalar(parsed, 0x829D, IfdKind::Exif);
    const std::string iso = findScalar(parsed, 0x8827, IfdKind::Exif);
    if (!make.empty()) os << ", Make=" << make;
    if (!model.empty()) os << ", Model=" << model;
    if (!date.empty()) os << ", DateTimeOriginal=" << date;
    if (!exposure.empty()) os << ", ExposureTime=" << exposure;
    if (!fnum.empty()) os << ", FNumber=" << fnum;
    if (!iso.empty()) os << ", ISO=" << iso;
    if (!parsed.detectedMaker.empty()) os << ", MakerNote=" << parsed.detectedMaker;
    if (!parsed.warnings.empty()) os << ", " << parsed.warnings.size() << " warning(s)";
    return os.str();
}

std::vector<ThumbnailRef> findThumbnails(const ParseResult& parsed) {
    std::vector<ThumbnailRef> out;
    // F5: a negative, non-finite or out-of-range double must never reach a
    // static_cast<std::size_t> (undefined behaviour).
    auto toSize = [](double v, bool ok, std::size_t& dst) {
        if (!ok || !std::isfinite(v) || v < 0.0 || v >= 18446744073709551616.0) return false;
        dst = static_cast<std::size_t>(v);
        return true;
    };
    std::vector<const IfdNode*> pending;
    for (const IfdNode& r : parsed.roots) pending.push_back(&r);
    for (const IfdNode* root : pending) {
        std::vector<const IfdNode*> stack{root};
        while (!stack.empty()) {
            const IfdNode* n = stack.back();
            stack.pop_back();
            std::size_t off = 0, len = 0;
            bool haveOff = false, haveLen = false;
            for (const TagEntry& t : n->tags) {
                bool ok = false;
                const double v = rawDoubleAt(t, parsed.header.endian, 0, ok);
                if (t.tag == 0x0201 && t.count >= 1) {
                    haveOff = toSize(v, ok, off);
                } else if (t.tag == 0x0202 && t.count >= 1) {
                    haveLen = toSize(v, ok, len);
                }
            }
            if (haveOff && haveLen && len > 0 &&
                off <= std::numeric_limits<std::size_t>::max() - parsed.tiffBaseAbs) {
                ThumbnailRef ref;
                ref.offset = parsed.tiffBaseAbs + off;
                ref.length = len;
                ref.label = n->name + " JPEGInterchangeFormat";
                out.push_back(ref);
            }
            for (const IfdNode& c : n->children) stack.push_back(&c);
        }
    }
    return out;
}

}  // namespace exif
