#!/usr/bin/env sh
set -eu
cd "$(dirname "$0")/../.."
mode=both
case "${1:-}" in
    --both) shift ;;
    --threaded) mode=threaded; shift ;;
    --single-threaded) mode=single; shift ;;
    --help|-h) echo "usage: $0 [--both|--threaded|--single-threaded] [pikmin-2-disc-image]"; exit 0 ;;
    --*) echo "unknown build mode: $1" >&2; exit 2 ;;
esac
if [ "$#" -gt 1 ]; then echo "provide one pikmin 2 disc image" >&2; exit 2; fi
if [ "$(em++ -dumpversion)" != "6.0.9" ]; then echo "use the emscripten 6.0.9 sdk" >&2; exit 2; fi
rom=${1:-}
if [ -n "$rom" ] && [ ! -f "$rom" ]; then echo "disc image does not exist: $rom" >&2; exit 2; fi
jobs=${nectar_web_jobs:-4}
case "$jobs" in ''|*[!0-9]*) echo "nectar_web_jobs must be a positive integer" >&2; exit 2 ;; esac
if ! [ "$jobs" -gt 0 ]; then echo "nectar_web_jobs must be a positive integer" >&2; exit 2; fi
build_mode() {
    threads=$1
    label=$2
    output="${nectar_web_pikmin2_build_dir:-build-web-pikmin2}-$3"
    emcmake cmake -S games/pikmin2 -B "$output" -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_TESTING=OFF -DPIKMIN2_GAME_VERSION=VERSION_GPVP01 "-DPIKMIN_WEB_THREADS=$threads"
    cmake --build "$output" --parallel "$jobs"
    emcmake cmake -S games/pikmin2 -B "$output/usa-engine" -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_TESTING=OFF -DPIKMIN2_GAME_VERSION=VERSION_GPVE01 "-DPIKMIN_WEB_THREADS=$threads"
    cmake --build "$output/usa-engine" --parallel "$jobs"
    cp "$output/usa-engine/bin/nectar.html" "$output/bin/nectar.html"
    cp "$output/usa-engine/bin/nectar.js" "$output/bin/nectar.js"
    cp "$output/usa-engine/bin/nectar.wasm" "$output/bin/nectar.wasm"
    cp "$output/usa-engine/bin/nectar-build.json" "$output/bin/nectar-build.json"
    python3 - "$output/bin/nectar-build.json" <<'UPDATE'
import json, pathlib, sys
path = pathlib.Path(sys.argv[1])
build = json.loads(path.read_text())
build['runtimes']['GPVP01:0'] = 'nectar-pal.js'
path.write_text(json.dumps(build) + '\n')
UPDATE
    if [ -n "$rom" ]; then
        python3 packaging/web/package_dist.py "$output/bin" "${nectar_web_dist_dir:-dist}/pikmin2-$label" --rom "$rom" --replace
    else
        python3 packaging/web/package_dist.py "$output/bin" "${nectar_web_dist_dir:-dist}/pikmin2-$label" --replace
    fi
}
case "$mode" in
    threaded) build_mode ON threaded threaded ;;
    single) build_mode OFF single-threaded single ;;
    both) build_mode OFF single-threaded single; build_mode ON threaded threaded ;;
esac
