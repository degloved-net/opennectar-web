#include "nectar_net/secure_random.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace nectar::net {

bool secureRandomBytes(std::uint8_t* destination, std::size_t size)
{
    if (size == 0) return true;
    if (destination == nullptr) return false;
#ifdef _WIN32
    while (size != 0) {
        const ULONG chunk = size > 0xffffffffULL ? 0xffffffffUL : static_cast<ULONG>(size);
        if (BCryptGenRandom(nullptr, destination, chunk, BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) return false;
        destination += chunk;
        size -= chunk;
    }
    return true;
#else
    int descriptor;
    do {
        descriptor = ::open("/dev/urandom", O_RDONLY
#ifdef O_CLOEXEC
                            | O_CLOEXEC
#endif
        );
    } while (descriptor < 0 && errno == EINTR);
    if (descriptor < 0) return false;

    bool ok = true;
    while (size != 0) {
        const ssize_t count = ::read(descriptor, destination, size);
        if (count > 0) {
            destination += static_cast<std::size_t>(count);
            size -= static_cast<std::size_t>(count);
        } else if (count < 0 && errno == EINTR) {
            continue;
        } else {
            ok = false;
            break;
        }
    }
    // Do not retry close() after EINTR: on Linux the descriptor may already
    // be closed and a retry could close an unrelated descriptor reused by
    // another thread.
    if (::close(descriptor) < 0 && errno != EINTR) ok = false;
    return ok;
#endif
}

} // namespace nectar::net
