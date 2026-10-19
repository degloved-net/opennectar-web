#!/usr/bin/env bash
# Builds the pinned online transport dependencies (GameNetworkingSockets,
# protobuf and the crypto backend) as static libraries for one target.
#
#   scripts/netplay-deps.sh linux|mingw64|android-arm64|macos [install-prefix]
#
# macos se ejecuta en un Mac (brew install cmake ninja coreutils) y compila
# para la arquitectura de ese Mac.
#
# The prefix defaults to ~/.cache/open-nectar/netplay-deps/install/<target>.
#
# Sources are downloaded once into a cache outside the repository and checked
# against the SHA-256 recorded below before anything is extracted. Pins,
# licenses and the update procedure: games/pikmin1/third_party/NETPLAY_VENDOR.md.
# Players never run this: everything ends up linked inside the game binary.
#
# Then configure the game or nectar_net with
#   -DNECTAR_NET_ENABLE_GNS=ON -DCMAKE_PREFIX_PATH=<install-prefix>
set -euo pipefail

target="${1:-}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cache="${NECTAR_DEPS_CACHE:-${HOME}/.cache/open-nectar/netplay-deps}"
prefix="${2:-${cache}/install/${target}}"
jobs="$(nproc 2>/dev/null || echo 4)"
ndk_version="28.2.13676358"
android_api=29

openssl_version=3.5.5
openssl_sha=b28c91532a8b65a1f983b4c28b7488174e4a01008e29ce8e69bd789f28bc2a89
protobuf_version=3.21.12
protobuf_sha=4eab9b524aa5913c6fffb20b2a8abf5ef7f95a80bc0701f3a6dbb4c607f73460
gns_commit=d534c19aa760df3fb75fd20db13ba1932b8a5463
gns_sha=099d4b0ff1837f0f27c83d4ce05869ec1d32136866f69a3336cc137e951189ab
miniupnpc_version=2.3.3
miniupnpc_sha=d52a0afa614ad6c088cc9ddff1ae7d29c8c595ac5fdd321170a05f41e634bd1a

case "${target}" in
    linux|mingw64|android-arm64|macos) ;;
    *)
        printf 'Uso: %s linux|mingw64|android-arm64|macos [prefijo]\n' "$0" >&2
        exit 2
        ;;
esac

for tool in cmake ninja curl sha256sum tar make perl; do
    command -v "${tool}" >/dev/null 2>&1 || { printf 'Falta la herramienta: %s\n' "${tool}" >&2; exit 1; }
done

mkdir -p "${cache}"
work="${cache}/work/${target}"

fetch() { # name url sha256
    local file="${cache}/$1"
    if [[ ! -f "${file}" ]]; then
        curl -fL --retry 3 -o "${file}.part" "$2"
        mv "${file}.part" "${file}"
    fi
    if ! printf '%s  %s\n' "$3" "${file}" | sha256sum --check --quiet -; then
        printf 'Huella SHA-256 incorrecta en %s; bórralo y repite.\n' "${file}" >&2
        exit 1
    fi
}

extract() { # archive directory
    rm -rf "${work}/$2"
    mkdir -p "${work}"
    tar -xzf "${cache}/$1" -C "${work}"
}

fetch "openssl-${openssl_version}.tar.gz" \
    "https://github.com/openssl/openssl/releases/download/openssl-${openssl_version}/openssl-${openssl_version}.tar.gz" \
    "${openssl_sha}"
fetch "protobuf-cpp-${protobuf_version}.tar.gz" \
    "https://github.com/protocolbuffers/protobuf/releases/download/v${protobuf_version#3.}/protobuf-cpp-${protobuf_version}.tar.gz" \
    "${protobuf_sha}"
fetch "GameNetworkingSockets-${gns_commit:0:8}.tar.gz" \
    "https://github.com/ValveSoftware/GameNetworkingSockets/archive/${gns_commit}.tar.gz" \
    "${gns_sha}"
fetch "miniupnpc-${miniupnpc_version}.tar.gz" \
    "https://miniupnp.tuxfamily.org/files/miniupnpc-${miniupnpc_version}.tar.gz" \
    "${miniupnpc_sha}"

# Toolchain arguments shared by the CMake builds.
cmake_target=()
case "${target}" in
    mingw64)
        command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1 \
            || { printf 'Instala g++-mingw-w64-x86-64.\n' >&2; exit 1; }
        # GNS writes <Windows.h>; MinGW headers on a case-sensitive file
        # system are lower case. A one-line forwarding header avoids patching GNS.
        shim="${work}/case-shim"
        mkdir -p "${shim}"
        printf '#pragma once\n#include <windows.h>\n' > "${shim}/Windows.h"
        # The game toolchain only searches the MinGW sysroot; let it also see
        # the dependencies installed into the prefix.
        cat > "${work}/toolchain-mingw64-deps.cmake" <<TOOLCHAIN
include("${root}/games/pikmin1/cmake/toolchain-mingw64.cmake")
list(APPEND CMAKE_FIND_ROOT_PATH "${prefix}")
TOOLCHAIN
        cmake_target=(-DCMAKE_TOOLCHAIN_FILE="${work}/toolchain-mingw64-deps.cmake"
                      -DCMAKE_CXX_FLAGS="-isystem ${shim}")
        ;;
    android-arm64)
        ndk="${ANDROID_NDK_ROOT:-${HOME}/Android/Sdk/ndk/${ndk_version}}"
        [[ -f "${ndk}/build/cmake/android.toolchain.cmake" ]] \
            || { printf 'No encuentro el NDK %s; define ANDROID_NDK_ROOT.\n' "${ndk_version}" >&2; exit 1; }
        cmake_target=(-DCMAKE_TOOLCHAIN_FILE="${ndk}/build/cmake/android.toolchain.cmake"
                      -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM="android-${android_api}")
        ;;
    macos)
        export MACOSX_DEPLOYMENT_TARGET="${MACOS_DEPLOYMENT_TARGET:-11.0}"
        cmake_target=(-DCMAKE_OSX_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET}")
        ;;
esac
cmake_common=(-G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="${prefix}"
              -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_POSITION_INDEPENDENT_CODE=ON
              -DCMAKE_FIND_ROOT_PATH="${prefix}")

# 1. protoc for the build machine: GNS generates its .pb.cc files with it, and
#    it must match the target runtime version exactly.
host_protoc="${cache}/host/protoc-${protobuf_version}/bin/protoc"
if [[ ! -x "${host_protoc}" ]]; then
    extract "protobuf-cpp-${protobuf_version}.tar.gz" "protobuf-${protobuf_version}"
    rm -rf "${work}/protoc-host"
    cmake -S "${work}/protobuf-${protobuf_version}" -B "${work}/protoc-host" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="${cache}/host/protoc-${protobuf_version}" \
        -Dprotobuf_BUILD_TESTS=OFF -Dprotobuf_WITH_ZLIB=OFF -Dprotobuf_BUILD_SHARED_LIBS=OFF
    cmake --build "${work}/protoc-host" --target protoc -j "${jobs}"
    mkdir -p "$(dirname "${host_protoc}")"
    cp "${work}/protoc-host/protoc" "${host_protoc}"
    chmod 755 "${host_protoc}"
fi

# 2. OpenSSL libcrypto (Linux, Android). Windows uses the system BCrypt.
if [[ "${target}" != mingw64 ]]; then
    extract "openssl-${openssl_version}.tar.gz" "openssl-${openssl_version}"
    (
        cd "${work}/openssl-${openssl_version}"
        options=(no-shared no-module no-tests no-docs no-dso no-engine no-legacy
                 --prefix="${prefix}" --libdir=lib)
        if [[ "${target}" == android-arm64 ]]; then
            export ANDROID_NDK_ROOT="${ndk}"
            export PATH="${ndk}/toolchains/llvm/prebuilt/linux-x86_64/bin:${PATH}"
            ./Configure android-arm64 -D__ANDROID_API__="${android_api}" "${options[@]}"
        elif [[ "${target}" == macos ]]; then
            ./Configure "darwin64-$(uname -m)-cc" "${options[@]}"
        else
            ./Configure linux-x86_64 "${options[@]}"
        fi
        make -j "${jobs}" build_libs
        make install_dev
    )
fi

# 3. protobuf runtime for the target (no protoc: that one is the host's).
extract "protobuf-cpp-${protobuf_version}.tar.gz" "protobuf-${protobuf_version}"
rm -rf "${work}/protobuf-build"
cmake -S "${work}/protobuf-${protobuf_version}" -B "${work}/protobuf-build" "${cmake_common[@]}" \
    "${cmake_target[@]}" -Dprotobuf_BUILD_TESTS=OFF -Dprotobuf_WITH_ZLIB=OFF \
    -Dprotobuf_BUILD_SHARED_LIBS=OFF -Dprotobuf_BUILD_PROTOC_BINARIES=OFF -Dprotobuf_BUILD_LIBPROTOC=OFF
cmake --build "${work}/protobuf-build" -j "${jobs}"
cmake --install "${work}/protobuf-build"

# 4. GameNetworkingSockets, static, without ICE/WebRTC/tools.
crypto=OpenSSL
[[ "${target}" == mingw64 ]] && crypto=BCrypt
extract "GameNetworkingSockets-${gns_commit:0:8}.tar.gz" "GameNetworkingSockets-${gns_commit}"
rm -rf "${work}/gns-build"
cmake -S "${work}/GameNetworkingSockets-${gns_commit}" -B "${work}/gns-build" "${cmake_common[@]}" \
    "${cmake_target[@]}" -DBUILD_STATIC_LIB=ON -DBUILD_SHARED_LIB=OFF -DBUILD_EXAMPLES=OFF \
    -DBUILD_TESTS=OFF -DBUILD_TOOLS=OFF -DENABLE_ICE=OFF -DUSE_STEAMWEBRTC=OFF -DLTO=OFF \
    -DUSE_CRYPTO="${crypto}" -DProtobuf_USE_STATIC_LIBS=ON -DCMAKE_PREFIX_PATH="${prefix}" \
    -DOPENSSL_ROOT_DIR="${prefix}" -DOPENSSL_USE_STATIC_LIBS=ON \
    -DProtobuf_PROTOC_EXECUTABLE="${host_protoc}" -DGNS_HOST_PROTOC="${host_protoc}"
cmake --build "${work}/gns-build" -j "${jobs}"
cmake --install "${work}/gns-build"
if [[ "${target}" == mingw64 ]]; then
    # GNS exports "Iphlpapi"; MinGW ships libiphlpapi.a and Linux is case
    # sensitive. Windows itself is not, so lower case is correct everywhere.
    sed -i 's/;Iphlpapi;/;iphlpapi;/' "${prefix}/lib/cmake/GameNetworkingSockets/GameNetworkingSockets.cmake"
fi

# 5. miniupnpc for UPnP IGD port mapping (the PCP/NAT-PMP client is ours).
extract "miniupnpc-${miniupnpc_version}.tar.gz" "miniupnpc-${miniupnpc_version}"
rm -rf "${work}/miniupnpc-build"
cmake -S "${work}/miniupnpc-${miniupnpc_version}" -B "${work}/miniupnpc-build" "${cmake_common[@]}" \
    "${cmake_target[@]}" -DUPNPC_BUILD_STATIC=ON -DUPNPC_BUILD_SHARED=OFF -DUPNPC_BUILD_TESTS=OFF \
    -DUPNPC_BUILD_SAMPLE=OFF
cmake --build "${work}/miniupnpc-build" -j "${jobs}"
cmake --install "${work}/miniupnpc-build"

printf '\nDependencias de red para %s instaladas en:\n  %s\n' "${target}" "${prefix}"
