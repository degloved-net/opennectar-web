#pragma once

#include <cstddef>
#include <cstdint>

namespace nectar::net::limits {

constexpr std::size_t kHeaderBytes                 = 42;
constexpr std::size_t kMaxUnreliableDatagramBytes = 1100;
constexpr std::size_t kMaxNormalPayloadBytes      = 65535;
constexpr std::size_t kMaxFullSnapshotBytes       = 32U * 1024U * 1024U;
constexpr std::size_t kMaxCompressedSnapshotBytes = 8U * 1024U * 1024U;
constexpr std::size_t kMaxCharacterPackageBytes   = 64U * 1024U * 1024U;
constexpr std::size_t kMaxCharacterFileBytes      = 32U * 1024U * 1024U;
constexpr std::size_t kMaxCharacterFileCount      = 128;
constexpr std::size_t kMaxUiStringBytes           = 256;
constexpr std::uint8_t kMaxInputsPerBundle        = 6;

} // namespace nectar::net::limits
