#!/usr/bin/env bash
# release-windows.sh [--clean]
#
# Archivos de Windows de una release de Open Nectar Fusion (ver
# scripts/release/make_release.py), compilados con MinGW desde Linux.
# SDL2 y el runtime de C/C++ van dentro de cada .exe: ninguna DLL propia.
#
# Las versiones salen de versions.json (raíz del repo).
set -euo pipefail

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${root}/build-release/windows"
out_dir="${root}/release-out/windows"
p1="${root}/games/pikmin1"
p2="${root}/games/pikmin2"

if [[ "${1:-}" == "--clean" ]]; then
    rm -rf "${build_dir}" "${out_dir}"
fi
if ! command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1; then
    printf 'Instala g++-mingw-w64-x86-64 (no está x86_64-w64-mingw32-g++).\n' >&2
    exit 1
fi
launcher_version="$(python3 -c "import json;print(json.load(open('${root}/versions.json'))['release'])")"
jobs="$(nproc)"

# PIKMIN_NATIVE_OPTIMIZE=OFF: -march=native compilaría para la CPU de esta
# máquina y el .exe moriría sin ventana ni log en CPUs sin esas instrucciones
# (issues #41 y #77). Igual que el release de Linux.
common=(-DCMAKE_BUILD_TYPE=Release -DPIKMIN_NATIVE_JAUDIO=ON -DPIKMIN_STATIC_RUNTIME=ON -DPIKMIN_NATIVE_OPTIMIZE=OFF)

# Online de Pikmin 1 fuera de las releases hasta que funcione: sin él, Co-op
# y VS son sólo locales y el título no ofrece «Online». NECTAR_RELEASE_ONLINE=1
# lo vuelve a compilar (mismo PIKMIN_NETPLAY_SOURCE_ID en todos los sistemas
# y regiones: así Windows, Linux, macOS y Android juegan entre sí).
mkdir -p "${build_dir}"
if [[ "${NECTAR_RELEASE_ONLINE:-0}" == "1" ]]; then
    deps="${NECTAR_DEPS_CACHE:-${HOME}/.cache/open-nectar/netplay-deps}/install/mingw64"
    [[ -d "${deps}" ]] || bash "${root}/scripts/netplay-deps.sh" mingw64 "${deps}"
    p1_toolchain="${build_dir}/toolchain-mingw64-online.cmake"
    cat > "${p1_toolchain}" <<TOOLCHAIN
include("${p1}/cmake/toolchain-mingw64.cmake")
list(APPEND CMAKE_FIND_ROOT_PATH "${deps}")
TOOLCHAIN
    online=(-DPIKMIN_NETPLAY_BUILD=ON -DNECTAR_ONLINE=ON -DNECTAR_NET_ENABLE_GNS=ON
            -DPIKMIN_NETPLAY_SOURCE_ID="release-${launcher_version}" -DCMAKE_PREFIX_PATH="${deps}")
else
    p1_toolchain="${p1}/cmake/toolchain-mingw64.cmake"
    online=(-DPIKMIN_NETPLAY_BUILD=OFF -DNECTAR_ONLINE=OFF)
fi

printf '[1/4] Pikmin 1 USA y el launcher...\n'
cmake -S "${p1}" -B "${build_dir}/p1-usa" "${common[@]}" "${online[@]}" \
    -DCMAKE_TOOLCHAIN_FILE="${p1_toolchain}" -DOPEN_NECTAR_VERSION="${launcher_version}"
cmake --build "${build_dir}/p1-usa" --target pikmin_pc pikmin_launcher nectar_mapmaker -j"${jobs}"

printf '[2/4] Pikmin 1 PAL...\n'
cmake -S "${p1}" -B "${build_dir}/p1-pal" "${common[@]}" "${online[@]}" \
    -DCMAKE_TOOLCHAIN_FILE="${p1_toolchain}" -DOPEN_NECTAR_VERSION="${launcher_version}" \
    -DPIKMIN_GAME_VERSION=VERSION_GPIP01_00
cmake --build "${build_dir}/p1-pal" --target pikmin_pc -j"${jobs}"

printf '[3/4] Pikmin 2...\n'
cmake -S "${p2}" -B "${build_dir}/p2" "${common[@]}" -DBUILD_TESTING=OFF \
    -DCMAKE_TOOLCHAIN_FILE="${p2}/cmake/toolchain-mingw64.cmake"
cmake --build "${build_dir}/p2" --target pikmin2_pc -j"${jobs}"
cmake -S "${p2}" -B "${build_dir}/p2-usa" "${common[@]}" -DBUILD_TESTING=OFF \
    -DCMAKE_TOOLCHAIN_FILE="${p2}/cmake/toolchain-mingw64.cmake" -DPIKMIN_GAME_VERSION=VERSION_GPVE01
cmake --build "${build_dir}/p2-usa" --target pikmin2_pc -j"${jobs}"

launcher="${build_dir}/p1-usa/bin/nectar-launcher.exe"
usa="${build_dir}/p1-usa/bin/nectar.exe"
pal="${build_dir}/p1-pal/bin/nectar.exe"
p2exe="${build_dir}/p2/pikmin2_pc.exe"
p2usa="${build_dir}/p2-usa/pikmin2_pc.exe"
mapmaker="${build_dir}/p1-usa/bin/nectar-mapmaker.exe"

printf '[4/4] Montando los archivos de la release...\n'
stripped="${build_dir}/stripped"
mkdir -p "${stripped}"
cp "${launcher}" "${stripped}/nectar-launcher.exe"
cp "${usa}" "${stripped}/nectar.exe"
cp "${pal}" "${stripped}/nectar-pal.exe"
cp "${p2exe}" "${stripped}/pikmin2_pc.exe"
cp "${p2usa}" "${stripped}/pikmin2_pc-usa.exe"
cp "${mapmaker}" "${stripped}/nectar-mapmaker.exe"
x86_64-w64-mingw32-strip "${stripped}"/*.exe

# Nada de DLL propias: cada .exe debe pedir solo DLL del sistema.
system_dlls='^(advapi32|comdlg32|gdi32|imm32|kernel32|msvcrt|ole32|oleaut32|opengl32|setupapi|shell32|user32|version|winmm|shlwapi|uuid|dwmapi|dinput8|xinput1_4|hid|cfgmgr32|ws2_32|bcrypt|iphlpapi|dbghelp)\.dll$'
for exe in "${stripped}"/*.exe; do
    while read -r dll; do
        if ! printf '%s\n' "${dll,,}" | grep -Eq "${system_dlls}"; then
            printf '%s necesita %s, que no viene con Windows. Revisa PIKMIN_STATIC_RUNTIME.\n' "${exe}" "${dll}" >&2
            exit 1
        fi
    done < <(x86_64-w64-mingw32-objdump -p "${exe}" | sed -n 's/.*DLL Name: //p')
done

python3 "${root}/scripts/release/make_release.py" --os windows --out "${out_dir}" \
    --launcher "${stripped}/nectar-launcher.exe" --p1-usa "${stripped}/nectar.exe" \
    --p1-pal "${stripped}/nectar-pal.exe" --p2 "${stripped}/pikmin2_pc.exe" --p2-usa "${stripped}/pikmin2_pc-usa.exe" \
    --mapmaker "${stripped}/nectar-mapmaker.exe" \
    --extra "${p1}/packaging/windows/README.txt"
