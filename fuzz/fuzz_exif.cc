// fuzz_exif.cc - libFuzzer harness for the exifreader parsers.
//
// Covers detectContainer() + parseTiff() + every renderer + thumbnail discovery.
// The first input byte selects an option profile, so one corpus exercises deep
// and shallow parses, value/raw rendering and the --no-makers / --no-thumbnails
// paths, plus several candidate TIFF base offsets.
//
// Build and run:
//   make fuzz CXX=clang++                       # 60 s on tests/fixtures
//   make fuzz CXX=clang++ FUZZ_SECONDS=600      # a longer campaign
//   cmake -S . -B build -DEXIFREADER_FUZZ=ON && cmake --build build --target exifreader_fuzz
//
// The reader has no third-party dependencies, so the only requirement is clang
// with the libFuzzer runtime.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "container.hpp"
#include "renderer.hpp"
#include "tiff_parser.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size == 0 || size > (4u << 20)) return 0;

    std::vector<std::uint8_t> buf(data, data + size);
    const std::uint8_t knob = buf[0];

    exif::ContainerInfo c = exif::detectContainer(buf.data(), buf.size());

    exif::ParseOptions opt;
    opt.parseMakerNote = (knob & 0x01) != 0;
    opt.parseThumbnailIfd = (knob & 0x02) != 0;
    opt.maxDepth = static_cast<int>(knob & 0x0F);                    // 0..15
    opt.maxNodes = 1 + static_cast<std::size_t>(knob >> 4) * 64;     // 1..961

    exif::ParseOptions def;   // library defaults
    std::vector<std::size_t> bases;
    for (const exif::EmbeddedExif& ee : c.exifCandidates) {
        if (ee.found) bases.push_back(ee.payloadOffset);
    }
    bases.push_back(0);
    if (size > 1) bases.push_back(size / 2);
    if (size > 8) bases.push_back(size - 8);
    bases.push_back(size - 1);

    for (std::size_t base : bases) {
        if (base >= size) continue;
        exif::ParseResult r1 = exif::parseTiff(buf.data(), buf.size(), base, opt);
        exif::ParseResult r2 = exif::parseTiff(buf.data(), buf.size(), base, def);

        exif::RenderOptions ro;
        ro.sourceName = "fuzz.jpg";
        ro.showValues = (knob & 0x04) != 0;
        ro.showRaw = (knob & 0x08) != 0;
        ro.showWarnings = true;
        volatile std::size_t sink = 0;
        sink += exif::renderTree(c, r1, ro).size();
        sink += exif::renderTags(c, r2, ro).size();
        sink += exif::renderJson(c, r1, ro).size();
        sink += exif::renderSummary(c, r2).size();
        std::vector<exif::ThumbnailRef> th = exif::findThumbnails(r1);
        for (const exif::ThumbnailRef& t : th) sink += t.offset + t.length;
        (void)sink;
    }
    return 0;
}
