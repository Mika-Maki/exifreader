// main.cpp - Image EXIF Information-Tree Reader command line interface.
//
//   exifreader [options] <image> [image ...]
//
// Reads the EXIF/TIFF metadata of JPEG, PNG, TIFF and HEIF/AVIF images and
// prints it as an information tree, a flat tag list or JSON.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "container.hpp"
#include "renderer.hpp"
#include "tiff_parser.hpp"
#include "tiff_tags.hpp"

namespace {

const char* kProgram = "exifreader";
const char* kVersion = "1.0.0";

struct Options {
    bool json = false;
    bool tags = false;
    bool summary = false;
    bool showValues = true;
    bool showRaw = false;
    bool parseMakerNote = true;
    bool parseThumbnail = true;
    bool showWarnings = true;
    int maxDepth = 8;
    std::size_t maxNodes = 200000;
    std::string extractDir;
    bool listTags = false;
    std::string listKind = "ifd0";
    std::vector<std::string> inputs;
};

void usage(std::ostream& os) {
    os << "Image EXIF Information-Tree Reader " << kVersion << "\n"
       << "Usage: " << kProgram << " [options] <image> [image ...]\n"
       << "\n"
       << "Reads EXIF/TIFF metadata from JPEG, PNG, TIFF and HEIF/AVIF files and\n"
       << "prints it as a navigable information tree (IFD -> tag -> value).\n"
       << "\n"
       << "Output modes\n"
       << "  -t, --tree            indented information tree (default)\n"
       << "  --tags                one tag per line: ifd/path | name (id) | value | type\n"
       << "  -j, --json            structured JSON (raw + formatted values)\n"
       << "  -s, --summary         one compact line per image\n"
       << "\n"
       << "Content selection\n"
       << "  --no-values           omit values, print names/types only\n"
       << "  --raw                 also dump raw tag bytes (hex)\n"
       << "  --no-makers           do not parse MakerNote blocks\n"
       << "  --no-thumbnails       do not follow the IFD1 thumbnail directory\n"
       << "  --no-warnings         hide parser warnings\n"
       << "  --max-depth N         IFD recursion limit (default 8, clamped to " << exif::kMaxIfdDepth << ")\n"
       << "  --max-nodes N         directory budget, guards against hostile files\n"
       << "  --extract DIR         write embedded thumbnails/previews into DIR\n"
       << "\n"
       << "Other\n"
       << "  --list-tags [KIND]    list the built-in dictionary; KIND is ifd0, ifd1,\n"
       << "                        exif, gps, interop, makernote or a vendor name\n"
       << "  -h, --help            this help\n"
       << "  -V, --version         version\n"
       << "\n"
       << "Exit status: 0 success, 1 usage/IO error, 2 parse error, 3 partial\n"
       << "             (EXIF found but the file reports anomalies).\n"
       << "\n"
       << "Examples\n"
       << "  " << kProgram << " photo.jpg\n"
       << "  " << kProgram << " --tags --no-values *.jpg\n"
       << "  " << kProgram << " --json photo.heic | jq .exif.ifds[0].tags\n"
       << "  " << kProgram << " --extract out photo.jpg && ls out\n";
}

std::string toLower(std::string s) {
    return exif::toLower(std::move(s));
}

bool readAll(const std::string& path, std::vector<std::uint8_t>& out, std::string& err) {
    if (path == "-") {
        std::istreambuf_iterator<char> begin(std::cin), end;
        out.assign(begin, end);
        if (out.empty()) {
            err = "no data on stdin";
            return false;
        }
        return true;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        err = "cannot open file";
        return false;
    }
    in.seekg(0, std::ios::end);
    const std::streamoff len = in.tellg();
    if (len > 0) {
        in.seekg(0, std::ios::beg);
        if (in) {
            out.resize(static_cast<std::size_t>(len));
            in.read(reinterpret_cast<char*>(out.data()), len);
            if (!in) {
                err = "short read";
                out.clear();
                return false;
            }
            return true;
        }
    }
    // F8: the size is unknown or zero (procfs, FIFOs, empty files). Fall back
    // to a plain stream read instead of silently treating the input as empty.
    in.clear();
    in.seekg(0, std::ios::beg);
    in.clear();   // a failed seek (pipe) must not block the reads below
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (in.bad()) {
        err = "read error";
        out.clear();
        return false;
    }
    return true;
}

std::string baseName(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string twoDigits(int v) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d", v);
    return buf;
}

int processOne(const std::string& path, const Options& opt, std::string& output, int& thumbCount,
               std::string& err) {
    std::vector<std::uint8_t> data;
    if (!readAll(path, data, err)) return 1;

    const exif::ContainerInfo container = exif::detectContainer(data.data(), data.size());
    exif::ParseOptions popt;
    popt.parseMakerNote = opt.parseMakerNote;
    popt.parseThumbnailIfd = opt.parseThumbnail;
    popt.maxDepth = opt.maxDepth;
    popt.maxNodes = opt.maxNodes;

    exif::ParseResult parsed;
    if (!container.exifCandidates.empty() && container.exifCandidates.front().found) {
        const exif::EmbeddedExif& ee = container.exifCandidates.front();
        parsed = exif::parseTiff(data.data(), data.size(), ee.payloadOffset, popt);
    } else if (container.kind == exif::ContainerKind::Heif) {
        // Files produced by some writers put the Exif item outside iloc's
        // reach; fall back to a raw signature scan of the whole buffer.
        const std::uint8_t* p = data.data();
        std::size_t found = data.size();
        for (std::size_t i = 0; i + 10 < data.size(); ++i) {
            if (std::memcmp(p + i, "Exif\0\0", 6) == 0) {
                const std::size_t tiff = i + 6;
                if (tiff + 8 <= data.size() &&
                    ((p[tiff] == 'I' && p[tiff + 1] == 'I') || (p[tiff] == 'M' && p[tiff + 1] == 'M'))) {
                    found = tiff;
                    break;
                }
            }
        }
        if (found < data.size()) parsed = exif::parseTiff(data.data(), data.size(), found, popt);
    }

    exif::RenderOptions ropt;
    ropt.showValues = opt.showValues;
    ropt.showRaw = opt.showRaw;
    ropt.showWarnings = opt.showWarnings;
    ropt.sourceName = path == "-" ? "<stdin>" : path;

    if (opt.tags) output = exif::renderTags(container, parsed, ropt);
    else if (opt.json) output = exif::renderJson(container, parsed, ropt);
    else if (opt.summary) output = exif::renderSummary(container, parsed) + "\n";
    else output = exif::renderTree(container, parsed, ropt);

    if (!opt.extractDir.empty() && parsed.ok) {
        const std::vector<exif::ThumbnailRef> thumbs = exif::findThumbnails(parsed);
        for (const exif::ThumbnailRef& t : thumbs) {
            if (t.offset >= data.size() || t.length == 0) continue;
            const std::size_t len = std::min(t.length, data.size() - t.offset);
            if (len < 4) continue;
            const std::uint8_t* p = data.data() + t.offset;
            std::string ext = ".bin";
            if (p[0] == 0xFF && p[1] == 0xD8) ext = ".jpg";
            else if (len > 8 && std::memcmp(p, "\x89PNG\r\n\x1a\n", 8) == 0) ext = ".png";
            else if ((p[0] == 'I' && p[1] == 'I') || (p[0] == 'M' && p[1] == 'M')) ext = ".tif";
            const std::string stem = baseName(ropt.sourceName);
            const std::string outPath = opt.extractDir + "/" + stem + ".thumb" +
                                        twoDigits(++thumbCount) + ext;
            std::ofstream out(outPath, std::ios::binary);
            if (!out) {
                err = "cannot write " + outPath;
                return 1;
            }
            out.write(reinterpret_cast<const char*>(p), static_cast<std::streamsize>(len));
            output += "extracted thumbnail -> " + outPath + " (" + std::to_string(len) + " bytes)\n";
        }
    }

    if (!parsed.ok) return 2;
    if (!parsed.warnings.empty()) return 3;
    return 0;
}

int listTags(const Options& opt) {
    const std::string kind = toLower(opt.listKind);
    exif::IfdKind k = exif::IfdKind::Ifd0;
    const char* maker = "";
    if (kind == "ifd0" || kind == "tiff" || kind == "ifd") k = exif::IfdKind::Ifd0;
    else if (kind == "ifd1") k = exif::IfdKind::Ifd1;
    else if (kind == "exif" || kind == "exififd") k = exif::IfdKind::Exif;
    else if (kind == "gps") k = exif::IfdKind::Gps;
    else if (kind == "interop") k = exif::IfdKind::Interop;
    else { 
        k = exif::IfdKind::MakerNote;
        maker = opt.listKind.c_str();
    }
    std::size_t count = 0;
    const exif::TagInfo* table = exif::tagTableFor(k, maker, count);
    if (!table || count == 0) {
        std::cerr << kProgram << ": no dictionary for '" << opt.listKind << "'\n";
        return 1;
    }
    std::vector<const exif::TagInfo*> sorted;
    sorted.reserve(count);
    for (std::size_t i = 0; i < count; ++i) sorted.push_back(&table[i]);
    std::sort(sorted.begin(), sorted.end(),
              [](const exif::TagInfo* a, const exif::TagInfo* b) { return a->id < b->id; });
    std::printf("dictionary: %s (%zu tags)\n", opt.listKind.c_str(), count);
    for (const exif::TagInfo* t : sorted) {
        std::printf("  0x%04X  %-42s %s\n", t->id, t->name, t->comment);
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    Options opt;
    std::ios::sync_with_stdio(false);

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto needValue = [&](const char* what) -> const char* {
            if (i + 1 >= argc) {
                std::cerr << kProgram << ": option " << what << " needs a value\n";
                std::exit(1);
            }
            return argv[++i];
        };
        if (a == "-h" || a == "--help") { usage(std::cout); return 0; }
        if (a == "-V" || a == "--version") { std::cout << kProgram << " " << kVersion << "\n"; return 0; }
        if (a == "-j" || a == "--json") { opt.json = true; continue; }
        if (a == "-t" || a == "--tree") { opt.json = opt.tags = opt.summary = false; continue; }
        if (a == "--tags" || a == "--list") { opt.tags = true; continue; }
        if (a == "-s" || a == "--summary") { opt.summary = true; continue; }
        if (a == "--no-values") { opt.showValues = false; continue; }
        if (a == "--raw") { opt.showRaw = true; continue; }
        if (a == "--no-makers" || a == "--no-makernote") { opt.parseMakerNote = false; continue; }
        if (a == "--no-thumbnails" || a == "--no-thumb") { opt.parseThumbnail = false; continue; }
        if (a == "--no-warnings") { opt.showWarnings = false; continue; }
        if (a == "--max-depth") {
            // F6: clamp to a depth the stack survives; a huge value used to be
            // passed straight through and overflow the stack on a deep chain.
            const long requested = std::strtol(needValue("--max-depth"), nullptr, 10);
            const long clamped = std::max<long>(0, std::min<long>(requested, exif::kMaxIfdDepth));
            if (clamped != requested) {
                std::cerr << kProgram << ": --max-depth clamped to " << clamped
                          << " (safe maximum " << exif::kMaxIfdDepth << ")\n";
            }
            opt.maxDepth = static_cast<int>(clamped);
            continue;
        }
        if (a == "--max-nodes") { opt.maxNodes = static_cast<std::size_t>(std::strtoull(needValue("--max-nodes"), nullptr, 10)); continue; }
        if (a == "--extract") { opt.extractDir = needValue("--extract"); continue; }
        if (a == "--list-tags") {
            opt.listTags = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') opt.listKind = argv[++i];
            continue;
        }
        if (a == "--") {
            for (++i; i < argc; ++i) opt.inputs.push_back(argv[i]);
            break;
        }
        if (!a.empty() && a[0] == '-' && a != "-") {
            std::cerr << kProgram << ": unknown option '" << a << "' (try --help)\n";
            return 1;
        }
        opt.inputs.push_back(a);
    }

    if (opt.listTags) return listTags(opt);
    if (opt.inputs.empty()) {
        usage(std::cerr);
        return 1;
    }

    int exitCode = 0;
    int thumbCount = 0;
    for (const std::string& path : opt.inputs) {
        std::string output;
        std::string err;
        const int rc = processOne(path, opt, output, thumbCount, err);
        if (rc == 1) {
            std::cerr << kProgram << ": " << path << ": " << err << "\n";
            exitCode = std::max(exitCode, 1);
            continue;
        }
        std::cout << output;
        if (!output.empty() && output.back() != '\n') std::cout << "\n";
        exitCode = std::max(exitCode, rc);
    }
    std::cout.flush();
    return exitCode;
}
