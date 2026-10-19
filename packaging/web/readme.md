# web port

the browser build shares the native engine, renderer, and settings. both games keep upstream’s layout in `games/pikmin1` and `games/pikmin2`. shared browser support lives in `packaging/web`, including its native runtime in `packaging/web/runtime`.

## pikmin 2

the engines are updated from open nectar 1.0.2 at `5d898ac6`. both game source directories match upstream fusion. browser storage names for pikmin 1 are preserved. pikmin 2 has separate saves, imported discs, texture libraries, and save backups on the same origin.

build pikmin 2 in both browser modes with your gamecube usa or european disc:

```sh
./packaging/web/build_pikmin2.sh --both "/path/to/pikmin2.rvz"
npx --yes serve dist/pikmin2-single-threaded -l tcp://127.0.0.1:8001
python3 dist/pikmin2-threaded/serve.py --open --port 8002
```

a separate pikmin 2 cdn launcher can be packaged from the single-threaded engine:

```sh
python3 packaging/web/package_jsdelivr.py build-web-pikmin2-single/bin dist/pikmin2-jsdelivr \
  --cdn-root 'https://cdn.jsdelivr.net/gh/owner/repo@tag/dist/pikmin2-jsdelivr/cdn' \
  --rom "/path/to/pikmin2.rvz"
```

omit the disc path for upload-only distributions. the build includes matching usa revision 0 (`GPVE01:0`) and european revision 0 (`GPVP01:0`) engines. both distributions retain the `index.html`, `assets/`, and `serve.py` layout, and included discs use the same 19,000,000-byte parts. `--single-threaded` and `--threaded` build one mode. `nectar_web_pikmin2_build_dir` overrides the build directory prefix.

pikmin 2 preserves the native section loop with asyncify and yields at each retrace. the single-threaded version runs sdk tasks as cooperative fibers; the threaded version runs them through browser pthreads. the shared browser menu integration provides save export and streaming texture packs. browser builds are still experimental. the usa disc reaches the first area in both modes, with movie audio and save-file creation working. visible brightness artifacts remain, and a full playthrough has not been verified.

after running `test.sh`, browser tests can check both pikmin 2 modes, storage isolation, and the single-file launcher. they use synthetic disc assets and require `puppeteer-core` and chrome:

```sh
node packaging/web/tests/pikmin2-browser.cjs
NECTAR_TEST_GAME=pikmin2 NECTAR_TEST_SINGLE_BUILD=build-web-pikmin2-single/bin node packaging/web/tests/jsdelivr-browser.cjs
```

## build

when switching from the former root source layout, use a fresh `nectar_web_build_dir` or remove the old generated build directory before rebuilding. cmake caches the source path.

use emscripten 6.0.9, a current stable rust toolchain, node.js, python 3, and cmake. enable the rust target once:

```sh
rustup target add wasm32-unknown-emscripten
```

with the emscripten sdk active, run from the repository root:

```sh
./packaging/web/build.sh
```

this keeps the existing threaded default in `build-web/bin`. it requires coop and coep response headers for browser shared memory; the included launcher sets them automatically.

build a single-threaded copy that works on an ordinary static server without isolation headers:

```sh
./packaging/web/build.sh --single-threaded
```

its output is `build-web-single/bin`. console tasks yield cooperatively on one browser thread, including loading and memory-card workers. movie decoding also yields between updates, and browser audio-device opening runs on the same thread. the renderer and game features are shared; asyncify enables the cooperative task stacks, with additional runtime overhead compared with pthreads.

to compile both variants:

```sh
./packaging/web/build.sh --both
```

the distributable outputs are `dist/threaded` and `dist/single-threaded`. each contains only `index.html`, `assets/`, and `serve.py` at the top level. keep the assets folder with its page. intermediate compiler files remain in `build-web-threaded` and `build-web-single`; they are not the distribution. these are separate wasm binaries, with no automatic mode switching. the browser loader reads the build manifest to check the requirements of that build. `nectar_web_dist_dir` overrides the distribution parent directory.

these builds contain both usa rev. 1 and european engines. building without an image makes an upload-only package. open the page and select **upload rom**, or drop your disc image onto the chooser. supported containers are iso/gcm, rvz, wia, gcz, ciso, tgc, and wbfs; the contents must be pikmin usa rev. 1 (`GPIE01`, revision 1) or europe (`GPIP01`, revision 0). wii/new play control discs and other revisions are rejected with a message.

```sh
npx --yes serve dist/single-threaded -l tcp://127.0.0.1:8000

python3 dist/threaded/serve.py --open
```

the image is decoded locally in an ordinary web worker, without uploading it. only `dataDir` and `MovieData` are extracted. imported files are saved in indexeddb for this browser and origin; on later visits select **play saved rom**. keep the same hostname and port to reuse imports and saves. clearing site data removes both. a canceled or failed import keeps the previous copy. allow enough browser storage for the extracted files and memory for the running game.

to include a split rom in both distributions, pass your image directly:

```sh
nectar_web_build_dir=build-private ./packaging/web/build.sh --both "/path/to/pikmin.rvz"
npx --yes serve dist/single-threaded -l tcp://127.0.0.1:8000
```

the chooser offers **play** alongside **upload rom**. each distribution includes the original image as 19,000,000-byte parts in `assets/rom/`, with sizes and sha256 hashes in `assets/nectar-build.json`. the loader downloads at most two parts at once and joins blobs without allocating a full-image arraybuffer. it verifies every part, imports through the existing worker, and saves the extracted files for later visits. cancellation or a damaged part leaves the previous import intact. the original rom is read without modification.

to add an image to already compiled engines without recompiling:

```sh
python3 packaging/web/package_dist.py build-web-single/bin dist/single-threaded --rom '/path/to/pikmin.rvz' --replace
python3 packaging/web/package_dist.py build-web-threaded/bin dist/threaded --rom '/path/to/pikmin.rvz' --replace
```

`--part-bytes` adjusts the part size up to 20,000,000 bytes. keep the complete assets folder with its page. you can still pass an extracted `dataDir` to the build script for the older **play included copy** package. the decoder is built from pinned [nod](https://github.com/encounter/nod) sources; its third-party license notices ship in `nectar-disc-licenses.txt`.

stop one server before starting the other on the same port. `serve.py` works for either variant and sets isolation headers for threaded builds. serve the html through http, rather than opening it as a file. saves belong to the browser and origin; both variants share saves when served from the same hostname and port.

the data folder must contain `parms/gamePrms.bin`. keep game data out of source control. `nectar_web_jobs` sets the compile job count (default: 4). `nectar_web_build_dir` overrides the output directory for one variant, or supplies the prefix for the `-threaded` and `-single` directories with `--both`. direct cmake builds use `-DPIKMIN_WEB_THREADS=ON` or `OFF`; use a separate build directory for each mode.

game assets are packaged separately and reused when their contents have not changed. keep playable builds in a separate output directory: building without an asset path removes any old asset package from that build directory.

run `./packaging/web/test.sh` with the sdk active to test cooperative message queues, locks, joins, cancellation, stack alignment, real card-worker save writes, and loader mode checks without game data. the card test writes and reads back game progress and options, with a timeout to catch frozen save operations. disc tests use synthetic file tables and compare extracted bytes; if dolphintool is installed they also cover compressed rvz, wia, and gcz containers.


## upstream updates

```sh
git fetch upstream
git merge upstream/main
```

the optional browser integration test uses `puppeteer-core` and chrome. after running the normal tests, serve `build-web-single/bin` on port 8765 without custom headers, then run `node packaging/web/tests/browser.cjs`. install puppeteer core in a temporary tools directory and expose it via `NODE_PATH`; set `NECTAR_TEST_CHROME` if chrome is elsewhere. this test initializes the real engine filesystem with synthetic files, skips the game entry point, and verifies import, caching, region selection, cancellation, recovery, and save export downloads. full gameplay verification requires your own supported disc.

run `node packaging/web/tests/card-browser.cjs` with the same tools to test actual save writes and restoration after a browser reload. it starts its own temporary server without isolation headers and uses the runtime's browser storage setup with synthetic card data.

run `node packaging/web/tests/split-rom-browser.cjs` to check included rom parts in both regular distributions. it creates temporary packages from a synthetic rvz and verifies bounded downloads, cached reloads, upload selection, and preservation of the previous copy after corruption or cancellation.

run `node packaging/web/tests/packs-browser.cjs` to verify opfs imports, migration from idbfs, save isolation, native png/dds/mip decoding, cache eviction, stale read cancellation, import rollback, and reloads. it uses synthetic files and a recorded gl upload target. set `NECTAR_TEST_LARGE_PACK=1` to also stream a 558 mib extracted pack and verify its files stay out of the runtime's ram filesystem.

## local html with jsdelivr

this is a separate single-threaded package. the ordinary distributions retain their opfs texture storage and normal server requirements.

```sh
python3 packaging/web/package_jsdelivr.py build-web-single/bin dist/jsdelivr \
  --cdn-root 'https://cdn.jsdelivr.net/gh/owner/repo@v1.0.0/nectar' \
  --rom '/path/to/pikmin.rvz'
```

the output has `index.html` and `cdn/`. publish the contents of `cdn/` at the repository path in the url, using that exact tag or commit. share only `index.html` with players; they can open it directly without a local server. omit `--rom` to make an upload-only launcher. `--replace` replaces a previously generated package after the new one is complete. no upload or repository push is performed by this tool.

the rom is split into 19,000,000-byte parts by default. `--part-bytes` can choose another size up to 20,000,000 bytes. runtime files and each part have sha256 hashes embedded in the html; mismatched downloads are rejected. keep the pinned runtime and launcher together when updating. the package uses the normal disc decoder and supports the same regions and formats.

the launcher caches parts as indexeddb blobs, composes the disc without a full-image arraybuffer, and imports game files in acknowledged 1 mib writes. only two downloads run at once. workers decode images and unpack textures; the page owns indexeddb because local blob workers cannot reliably access browser storage. texture payloads live in indexeddb chunks, with the same 32 mib working cache and native decoder used by the regular builds. save data and restore staging are namespaced to the repository path, so changing the pinned revision does not create a new save database.

chrome has been tested with a real `file://` page, cross-origin downloads, cached reloads, save export/restoration, both engines, native texture uploads, corruption rollback, and cancellation using synthetic discs. other browsers need testing; private browsing and storage quotas still apply. the game engine and extracted game assets still use ram. full gameplay verification requires a supported disc.

run `node packaging/web/tests/jsdelivr-browser.cjs` after the normal test script to repeat those checks. the fixture serves only synthetic files from a temporary cors server to stand in for the cdn; the page itself is opened directly from disk. run `python3 packaging/web/tests/package_test.py` to check the package layouts, chunk manifests, and input preservation.
