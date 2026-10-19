#pragma once

#include <cstddef>
#include <cstdint>

namespace nectar::net {

// Fills the destination from the operating system CSPRNG. There is no weak
// fallback: false means callers must abort creation of the session secret.
bool secureRandomBytes(std::uint8_t* destination, std::size_t size);

} // namespace nectar::net
