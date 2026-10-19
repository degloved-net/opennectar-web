#!/usr/bin/env bash
# Se ejecuta DENTRO del contenedor open-nectar-build (Ubuntu 20.04, glibc 2.31).
# Lo lanza scripts/release-linux.sh con la raíz de Fusion montada en /src; no
# llamarlo a mano.
#
# Deja en build-release/linux/:
#   p1-usa/bin/nectar            Pikmin 1 USA
#   p1-usa/bin/nectar-launcher   launcher
#   p1-pal/bin/nectar            Pikmin 1 PAL
#   p2/pikmin2_pc                Pikmin 2 PAL
#   p2-usa/pikmin2_pc            Pikmin 2 USA
# con SDL2 y el runtime de C++ dentro (solo dependen de glibc, libGL y libX11).
set -euo pipefail

src=/src
out="${src}/build-release/linux"
version="${OPEN_NECTAR_VERSION:?falta OPEN_NECTAR_VERSION}"
jobs="$(nproc)"

# SDL2 estática (fuentes oficiales: la copia de Android no trae Wayland).
sdl_version="${SDL_VERSION:-2.32.10}"
if [[ ! -f "${out}/sdl2/lib/libSDL2.a" ]]; then
    printf '%s\n' "[contenedor] Compilando SDL2 ${sdl_version} estática..."
    mkdir -p "${out}"
    if [[ ! -d "${out}/SDL2-${sdl_version}" ]]; then
        curl -fsSL "https://github.com/libsdl-org/SDL/releases/download/release-${sdl_version}/SDL2-${sdl_version}.tar.gz" \
            | tar -xz -C "${out}"
    fi
    cmake -S "${out}/SDL2-${sdl_version}" -B "${out}/sdl2-build" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_STATIC_PIC=ON -DSDL_TEST=OFF \
        -DCMAKE_INSTALL_PREFIX="${out}/sdl2"
    cmake --build "${out}/sdl2-build" -j"${jobs}"
    cmake --install "${out}/sdl2-build"
fi

common=(
    -G Ninja
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_PREFIX_PATH="${out}/sdl2"
    -DPIKMIN_NATIVE_JAUDIO=ON
    -DPIKMIN_STATIC_RUNTIME=ON
    # libGL.so.1 está en cualquier escritorio; libOpenGL.so.0 (GLVND) no siempre.
    -DOpenGL_GL_PREFERENCE=LEGACY
)

# Online de Pikmin 1 fuera de las releases hasta que funcione: sin él, Co-op
# y VS son sólo locales. NECTAR_RELEASE_ONLINE=1 lo vuelve a compilar (mismo
# PIKMIN_NETPLAY_SOURCE_ID en todos los sistemas y regiones; dependencias del
# transporte compiladas aquí, con la glibc del contenedor).
if [[ "${NECTAR_RELEASE_ONLINE:-0}" == "1" ]]; then
    deps="${out}/netplay-deps"
    if [[ ! -f "${deps}/.listo" ]]; then
        printf '%s\n' '[contenedor] Dependencias del online...'
        NECTAR_DEPS_CACHE="${out}/netplay-cache" bash "${src}/scripts/netplay-deps.sh" linux "${deps}"
        touch "${deps}/.listo"
    fi
    online=(-DPIKMIN_NETPLAY_BUILD=ON -DNECTAR_ONLINE=ON -DNECTAR_NET_ENABLE_GNS=ON
            -DPIKMIN_NETPLAY_SOURCE_ID="release-${version}" -DCMAKE_PREFIX_PATH="${out}/sdl2;${deps}")
else
    online=(-DPIKMIN_NETPLAY_BUILD=OFF -DNECTAR_ONLINE=OFF)
fi

printf '%s\n' '[contenedor] Pikmin 1 USA y el launcher...'
cmake -S "${src}/games/pikmin1" -B "${out}/p1-usa" "${common[@]}" "${online[@]}" \
    -DPIKMIN_NATIVE_OPTIMIZE=OFF -DPIKMIN_ENABLE_IPO=ON -DOPEN_NECTAR_VERSION="${version}"
cmake --build "${out}/p1-usa" --target pikmin_pc pikmin_launcher nectar_mapmaker -j"${jobs}"

printf '%s\n' '[contenedor] Pikmin 1 PAL...'
cmake -S "${src}/games/pikmin1" -B "${out}/p1-pal" "${common[@]}" "${online[@]}" \
    -DPIKMIN_NATIVE_OPTIMIZE=OFF -DPIKMIN_ENABLE_IPO=ON -DOPEN_NECTAR_VERSION="${version}" \
    -DPIKMIN_GAME_VERSION=VERSION_GPIP01_00
cmake --build "${out}/p1-pal" --target pikmin_pc -j"${jobs}"

printf '%s\n' '[contenedor] Pikmin 2...'
cmake -S "${src}/games/pikmin2" -B "${out}/p2" "${common[@]}" -DBUILD_TESTING=OFF
cmake --build "${out}/p2" --target pikmin2_pc -j"${jobs}"

printf '%s\n' '[contenedor] Pikmin 2 USA...'
cmake -S "${src}/games/pikmin2" -B "${out}/p2-usa" "${common[@]}" -DBUILD_TESTING=OFF \
    -DPIKMIN_GAME_VERSION=VERSION_GPVE01
cmake --build "${out}/p2-usa" --target pikmin2_pc -j"${jobs}"
