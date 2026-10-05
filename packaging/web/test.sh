#!/usr/bin/env sh
set -eu
cd "$(dirname "$0")/../.."
game=games/pikmin1
runtime=packaging/web/runtime

output=${nectar_web_test_dir:-build-web-tests}
mkdir -p "$output"
em++ -std=c++17 -O2 -Wno-register -DPIKI_PC_PORT=1 -include "$game/pc_port/pc_types.h" \
    -I"$game/include" -I"$game/pc_port" -I"$runtime" "$runtime/web/pc_web_threads.cpp" packaging/web/tests/threads.cpp \
    -sASYNCIFY=1 -sASSERTIONS=2 -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=8388608 \
    -sENVIRONMENT=node,web -o "$output/threads.js"
node -e 'globalThis.SharedArrayBuffer = undefined; require(require("path").resolve(process.argv[1]));' "$output/threads.js"
build_card_test() {
    card_output=$1
    shift
    em++ -std=c++17 -O2 -Wno-register -DPIKI_PC_PORT=1 -include "$game/pc_port/pc_types.h" \
        -I"$game/include" -I"$game/pc_port" -I"$runtime" "$runtime/web/pc_web_threads.cpp" "$game/pc_port/dolphin_stubs/card_stubs.cpp" \
        "$game/src/plugPikiColin/cardutil.cpp" packaging/web/tests/card.cpp \
        -sASYNCIFY=1 -sASSERTIONS=2 -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=8388608 \
        -sENVIRONMENT=node,web "$@" -o "$output/$card_output.js"
}
build_card_test card
node -e 'const result = require("node:child_process").execFileSync(process.execPath, ["-e", "globalThis.SharedArrayBuffer = undefined; require(process.argv[1]);", require("node:path").resolve(process.argv[1])], {timeout: 20000, encoding: "utf8", stdio: ["ignore", "pipe", "inherit"]}); process.stdout.write(result); require("node:assert/strict").match(result, /single-threaded card save tests passed/);' "$output/card.js"
build_card_test card-browser -lidbfs.js --pre-js packaging/web/pre.js -sEXPORTED_RUNTIME_METHODS=FS
em++ -std=c++17 -O2 -DPIKI_USE_GLES=1 -DPIKI_PC_PORT=1 -sUSE_SDL=2 \
    -include "$game/pc_port/pc_types.h" -I"$game/include" -I"$game/pc_port" -I"$runtime" -I"$game/third_party/stb" -I"$game/third_party/bcdec" \
    "$game/pc_port/gl/pc_texpack.cpp" packaging/web/tests/textures.cpp \
    -sALLOW_MEMORY_GROWTH=1 -sASSERTIONS=2 -sENVIRONMENT=web \
    -sEXPORTED_RUNTIME_METHODS=FS,ccall -lidbfs.js --pre-js packaging/web/pre.js \
    -o "$output/textures.js"
node packaging/web/tests/loader.cjs
python3 packaging/web/tests/serve_test.py
python3 packaging/web/tests/package_test.py

./packaging/web/build_disc.sh "$output/disc-runtime"
node packaging/web/tests/disc.cjs "$output/disc-runtime" "$output/discs"
