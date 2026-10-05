#!/usr/bin/env sh
set -eu

output=$1
target=${nectar_web_disc_build_dir:-build-web-disc}/target
case "$target" in
    /*) export CARGO_TARGET_DIR="$target" ;;
    *) export CARGO_TARGET_DIR="$PWD/$target" ;;
esac
export RUSTFLAGS='-C link-arg=-sUSE_ZLIB=1 -C link-arg=-sMODULARIZE=1 -C link-arg=-sEXPORT_NAME=NectarDisc -C link-arg=-sENVIRONMENT=worker,node -C link-arg=-sALLOW_MEMORY_GROWTH=1 -C link-arg=-sFORCE_FILESYSTEM=1 -C link-arg=-sEXIT_RUNTIME=0 -C link-arg=-lworkerfs.js -C link-arg=-lnodefs.js -C link-arg=-sEXPORTED_FUNCTIONS=_main,_nectar_disc_open,_nectar_disc_read,_nectar_disc_size,_nectar_disc_buffer,_nectar_disc_error,_nectar_disc_close -C link-arg=-sEXPORTED_RUNTIME_METHODS=FS,ccall,UTF8ToString,HEAPU8'
cargo build --locked --manifest-path packaging/web/disc/Cargo.toml --target wasm32-unknown-emscripten --release
mkdir -p "$output"
cp "$CARGO_TARGET_DIR/wasm32-unknown-emscripten/release/nectar-disc.js" "$output/nectar-disc.js"
cp "$CARGO_TARGET_DIR/wasm32-unknown-emscripten/release/nectar_disc.wasm" "$output/nectar_disc.wasm"
python3 packaging/web/disc/notices.py "$output/nectar-disc-licenses.txt"
