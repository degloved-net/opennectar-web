#include "nectar_net/byte_io.h"

#include <algorithm>
#include <limits>

namespace nectar::net {

ByteWriter::ByteWriter(std::size_t maxBytes)
    : mMaxBytes(maxBytes)
{
    mBytes.reserve(std::min<std::size_t>(maxBytes, 4096));
}

bool ByteWriter::reserveFor(std::size_t count)
{
    if (!mOk || count > mMaxBytes || mBytes.size() > mMaxBytes - count) {
        mOk = false;
        return false;
    }
    return true;
}

bool ByteWriter::writeU8(std::uint8_t value)
{
    if (!reserveFor(1)) return false;
    mBytes.push_back(value);
    return true;
}

bool ByteWriter::writeU16(std::uint16_t value)
{
    if (!reserveFor(2)) return false;
    mBytes.push_back(static_cast<std::uint8_t>(value >> 8));
    mBytes.push_back(static_cast<std::uint8_t>(value));
    return true;
}

bool ByteWriter::writeU32(std::uint32_t value)
{
    if (!reserveFor(4)) return false;
    for (int shift = 24; shift >= 0; shift -= 8) {
        mBytes.push_back(static_cast<std::uint8_t>(value >> shift));
    }
    return true;
}

bool ByteWriter::writeU64(std::uint64_t value)
{
    if (!reserveFor(8)) return false;
    for (int shift = 56; shift >= 0; shift -= 8) {
        mBytes.push_back(static_cast<std::uint8_t>(value >> shift));
    }
    return true;
}

bool ByteWriter::writeBytes(const std::uint8_t* data, std::size_t size)
{
    if (size == 0) return mOk;
    if (size != 0 && data == nullptr) {
        mOk = false;
        return false;
    }
    if (!reserveFor(size)) return false;
    mBytes.insert(mBytes.end(), data, data + size);
    return true;
}

ByteReader::ByteReader(const std::uint8_t* data, std::size_t size)
    : mData(data)
    , mSize(data == nullptr ? 0 : size)
{
}

ByteReader::ByteReader(const std::vector<std::uint8_t>& data)
    : ByteReader(data.data(), data.size())
{
}

bool ByteReader::has(std::size_t count) const
{
    return count <= mSize && mPosition <= mSize - count;
}

bool ByteReader::readU8(std::uint8_t& value)
{
    if (!has(1)) return false;
    value = mData[mPosition++];
    return true;
}

bool ByteReader::readU16(std::uint16_t& value)
{
    if (!has(2)) return false;
    value = static_cast<std::uint16_t>(mData[mPosition]) << 8 |
            static_cast<std::uint16_t>(mData[mPosition + 1]);
    mPosition += 2;
    return true;
}

bool ByteReader::readU32(std::uint32_t& value)
{
    if (!has(4)) return false;
    value = 0;
    for (int i = 0; i < 4; ++i) value = (value << 8) | mData[mPosition++];
    return true;
}

bool ByteReader::readU64(std::uint64_t& value)
{
    if (!has(8)) return false;
    value = 0;
    for (int i = 0; i < 8; ++i) value = (value << 8) | mData[mPosition++];
    return true;
}

bool ByteReader::readBytes(std::uint8_t* output, std::size_t size)
{
    if (size == 0) return true;
    if ((size != 0 && output == nullptr) || !has(size)) return false;
    std::copy(mData + mPosition, mData + mPosition + size, output);
    mPosition += size;
    return true;
}

} // namespace nectar::net
