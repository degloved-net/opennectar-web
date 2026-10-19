#pragma once

#include <cstddef>
#include <cstdint>

namespace nectar::net {

// Compares two buffers without branching on their contents, so the time taken
// does not reveal how many leading bytes of a secret matched.
bool constantTimeEqual(const std::uint8_t* a, const std::uint8_t* b, std::size_t size);

// Zeroes secret material in a way the optimiser may not remove.
void secureWipe(void* destination, std::size_t size);

} // namespace nectar::net
