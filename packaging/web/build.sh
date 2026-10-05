#!/usr/bin/env sh
set -eu
cd "$(dirname "$0")/../.."

mode=threaded
case "${1:-}" in
    --threaded) shift ;;
    --single-threaded) mode=single; shift ;;
    --both) mode=both; shift ;;
    --help|-h)
        echo "usage: $0 [--threaded|--single-threaded|--both] [disc-image-or-extracted-dataDir]"
        exit 0 ;;
    --*) echo "unknown build mode: $1" >&2; exit 2 ;;
esac
if [ "$#" -gt 1 ]; then
    echo "provide one disc image or extracted game-data directory" >&2
    exit 2
fi
if [ "$(em++ -dumpversion)" != "6.0.9" ]; then
    echo "use the emscripten 6.0.9 sdk to build the web port" >&2
    exit 2
fi

asset_dir=${1:-}
rom_file=
if [ -n "$asset_dir" ] && [ ! -d "$asset_dir" ]; then
    if [ ! -f "$asset_dir" ]; then
        echo "disc image or game-data directory does not exist: $asset_dir" >&2
        exit 2
    fi
    rom_file=$asset_dir
    asset_dir=
fi
jobs=${nectar_web_jobs:-4}
case "$jobs" in
    ''|*[!0-9]*) echo "nectar_web_jobs must be a positive integer" >&2; exit 2 ;;
esac
if ! [ "$jobs" -gt 0 ]; then
    echo "nectar_web_jobs must be a positive integer" >&2
    exit 2
fi

build_engine() {
    threads=$1
    output=$2
    version=$3
    assets=$4
    emcmake cmake -S games/pikmin1 -B "$output" \
        -DCMAKE_BUILD_TYPE=Release -DPIKMIN_BUILD_LAUNCHER=OFF \
        -DPIKMIN_BUILD_TESTS=OFF -DPIKMIN_NATIVE_OPTIMIZE=OFF -DPIKMIN_ENABLE_IPO=OFF \
        "-DPIKMIN_WEB_THREADS=$threads" "-DPIKMIN_WEB_ASSET_DIR=$assets" "-DPIKMIN_GAME_VERSION=$version"
    cmake --build "$output" --parallel "$jobs"
}

build_mode() {
    mode_threads=$1
    mode_output=$2
    build_engine "$mode_threads" "$mode_output" VERSION_GPIE01_01 "$asset_dir"
    build_engine "$mode_threads" "$mode_output/pal-engine" VERSION_GPIP01_00 ""
    cp "$mode_output/pal-engine/bin/nectar-pal.js" "$mode_output/bin/nectar-pal.js"
    cp "$mode_output/pal-engine/bin/nectar-pal.wasm" "$mode_output/bin/nectar-pal.wasm"
    python3 - "$mode_output/bin/nectar-build.json" <<'PY'
import json, pathlib, sys
path = pathlib.Path(sys.argv[1])
build = json.loads(path.read_text())
build['runtimes']['GPIP01:0'] = 'nectar-pal.js'
path.write_text(json.dumps(build) + '\n')
PY
    if [ "$mode_threads" = ON ]; then
        dist_mode=threaded
    else
        dist_mode=single-threaded
    fi
    if [ -n "$rom_file" ]; then
        python3 packaging/web/package_dist.py "$mode_output/bin" "${nectar_web_dist_dir:-dist}/$dist_mode" --rom "$rom_file" --replace
    else
        python3 packaging/web/package_dist.py "$mode_output/bin" "${nectar_web_dist_dir:-dist}/$dist_mode" --replace
    fi
}

case "$mode" in
    threaded) build_mode ON "${nectar_web_build_dir:-build-web}" ;;
    single) build_mode OFF "${nectar_web_build_dir:-build-web-single}" ;;
    both)
        build_mode ON "${nectar_web_build_dir:-build-web}-threaded"
        build_mode OFF "${nectar_web_build_dir:-build-web}-single" ;;
esac
