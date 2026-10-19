#include "nectar_net/secure_memory.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace nectar::net {

bool constantTimeEqual(const std::uint8_t* a, const std::uint8_t* b, std::size_t size)
{
    if (a == nullptr || b == nullptr) return false;
    volatile std::uint8_t difference = 0;
    for (std::size_t i = 0; i < size; ++i) difference = static_cast<std::uint8_t>(difference | (a[i] ^ b[i]));
    return difference == 0;
}

void secureWipe(void* destination, std::size_t size)
{
    if (destination == nullptr || size == 0) return;
#ifdef _WIN32
    SecureZeroMemory(destination, size);
#else
    volatile std::uint8_t* bytes = static_cast<volatile std::uint8_t*>(destination);
    for (std::size_t i = 0; i < size; ++i) bytes[i] = 0;
#if defined(__GNUC__) || defined(__clang__)
    __asm__ __volatile__("" : : "r"(destination) : "memory");
#endif
#endif
}

} // namespace nectar::net
