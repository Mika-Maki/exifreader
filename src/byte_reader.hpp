// byte_reader.hpp - bounded, checked view over an in-memory byte buffer.
#ifndef EXIFREADER_BYTE_READER_HPP
#define EXIFREADER_BYTE_READER_HPP

#include <cstdint>
#include <string>
#include <vector>

#include "exif_common.hpp"

namespace exif {

// A checked reader over a byte span. Every accessor is bounds checked and
// reports failure through its bool return value instead of throwing, so the
// parser can stay allocation-light and survive truncated/corrupt files.
class ByteReader {
public:
    ByteReader() = default;
    ByteReader(const std::uint8_t* data, std::size_t size, Endian e = Endian::Little)
        : data_(data), size_(size), endian_(e) {}

    void reset(const std::uint8_t* data, std::size_t size, Endian e) {
        data_ = data;
        size_ = size;
        endian_ = e;
    }

    const std::uint8_t* data() const { return data_; }
    std::size_t size() const { return size_; }
    Endian endian() const { return endian_; }

    // True when [off, off+len) lies fully inside the buffer.
    bool inRange(std::size_t off, std::size_t len) const {
        return off <= size_ && len <= size_ - off;
    }

    bool u8(std::size_t off, std::uint8_t& out) const {
        if (!inRange(off, 1)) return false;
        out = data_[off];
        return true;
    }
    bool u16(std::size_t off, std::uint16_t& out) const {
        if (!inRange(off, 2)) return false;
        out = static_cast<std::uint16_t>(readUInt(data_ + off, 2, endian_));
        return true;
    }
    bool u32(std::size_t off, std::uint32_t& out) const {
        if (!inRange(off, 4)) return false;
        out = readUInt(data_ + off, 4, endian_);
        return true;
    }
    bool i32(std::size_t off, std::int32_t& out) const {
        if (!inRange(off, 4)) return false;
        out = readInt(data_ + off, 4, endian_);
        return true;
    }
    // Raw copy out of the buffer; returns false when out of range.
    bool bytes(std::size_t off, std::size_t len, std::vector<std::uint8_t>& out) const {
        if (!inRange(off, len)) return false;
        out.assign(data_ + off, data_ + off + len);
        return true;
    }
    // Zero-copy access; nullptr when out of range.
    const std::uint8_t* ptr(std::size_t off, std::size_t len) const {
        return inRange(off, len) ? data_ + off : nullptr;
    }

private:
    const std::uint8_t* data_ = nullptr;
    std::size_t size_ = 0;
    Endian endian_ = Endian::Little;
};

}  // namespace exif

#endif  // EXIFREADER_BYTE_READER_HPP
