#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace nectar::net {

class ByteWriter {
public:
    explicit ByteWriter(std::size_t maxBytes);

    bool writeU8(std::uint8_t value);
    bool writeU16(std::uint16_t value);
    bool writeU32(std::uint32_t value);
    bool writeU64(std::uint64_t value);
    bool writeBytes(const std::uint8_t* data, std::size_t size);

    bool ok() const { return mOk; }
    const std::vector<std::uint8_t>& bytes() const { return mBytes; }

private:
    bool reserveFor(std::size_t count);

    std::vector<std::uint8_t> mBytes;
    std::size_t mMaxBytes;
    bool mOk = true;
};

class ByteReader {
public:
    ByteReader(const std::uint8_t* data, std::size_t size);
    explicit ByteReader(const std::vector<std::uint8_t>& data);

    bool readU8(std::uint8_t& value);
    bool readU16(std::uint16_t& value);
    bool readU32(std::uint32_t& value);
    bool readU64(std::uint64_t& value);
    bool readBytes(std::uint8_t* output, std::size_t size);

    std::size_t remaining() const { return mSize - mPosition; }
    std::size_t position() const { return mPosition; }

private:
    bool has(std::size_t count) const;

    const std::uint8_t* mData;
    std::size_t mSize;
    std::size_t mPosition = 0;
};

} // namespace nectar::net
