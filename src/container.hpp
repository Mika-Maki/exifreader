// container.hpp - locate the embedded EXIF/TIFF payload inside image files.
#ifndef EXIFREADER_CONTAINER_HPP
#define EXIFREADER_CONTAINER_HPP

#include <cstdint>
#include <string>
#include <vector>

#include "exif_common.hpp"

namespace exif {

enum class ContainerKind {
    Unknown,
    Jpeg,
    Png,
    Tiff,   // also DNG/NEF/CR2 style TIFF containers
    Heif,   // HEIC/HEIF/AVIF: ISO-BMFF with an 'Exif' item
};

const char* containerName(ContainerKind k);

struct EmbeddedExif {
    bool found = false;
    // Byte range of the TIFF header ("II*\0" / "MM\0*") inside the file buffer.
    std::size_t payloadOffset = 0;   // absolute offset of the TIFF header
    std::size_t payloadSize = 0;     // bytes available from payloadOffset
    std::string note;                // human readable provenance, e.g. "APP1 @0x12"
};

struct ContainerInfo {
    ContainerKind kind = ContainerKind::Unknown;
    std::string formatDetail;        // e.g. "JPEG (JFIF 1.1)", "HEIF (brand heic)"
    std::uint32_t width = 0;         // 0 = unknown
    std::uint32_t height = 0;
    std::string frameType;           // SOF variant for JPEG, IHDR for PNG
    bool hasXmp = false;
    std::vector<EmbeddedExif> exifCandidates;   // JPEG/PNG/HEIF sources
    std::vector<EmbeddedExif> extraPayloads;    // e.g. raw TIFF header
};

// Detect the container from magic bytes and fill in geometry + EXIF location.
// Never throws; unrecognised input yields kind == Unknown.
ContainerInfo detectContainer(const std::uint8_t* data, std::size_t size);

}  // namespace exif

#endif  // EXIFREADER_CONTAINER_HPP
