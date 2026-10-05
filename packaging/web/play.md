# open nectar web

## start

run `python3 serve.py --open` from this folder. python 3.9 or newer is required.

the helper starts a local server and opens the game in your browser. keep the terminal open and the runtime files together. opening the html file directly will not work.

threaded builds require browser shared memory and coop/coep isolation headers, which this helper supplies. single-threaded builds also work on ordinary static servers without those headers: run `npx --yes serve . -l tcp://127.0.0.1:8000` from this folder and open `http://127.0.0.1:8000/nectar.html`. all game files must be served from the same origin to avoid cross-origin fetch requirements. the build mode is recorded in `nectar-build.json`; keep it with the runtime files.

keep using the same browser and address, normally `http://127.0.0.1:8000/nectar.html`. saves and installed packs belong to that browser and origin. private browsing or clearing site data can remove them.

at startup, choose **upload rom** and select or drop your pikmin disc image. iso/gcm, rvz, wia, gcz, ciso, tgc, and wbfs containers are supported. the game must be pikmin usa rev. 1 or europe; the matching engine is selected automatically. imports run locally without uploading the image, including on single-threaded builds without isolation headers.

the browser saves the extracted files. on later visits select **play saved rom**. keep the same address to reuse it; private browsing or clearing site data can remove imports and saves. canceling an import or choosing an invalid image preserves the previous copy.

if this private build includes packaged game data, **play included copy** is also available. it is cached after the first download when browser storage has room.

## controls and display

f1 opens settings, f2 opens controls, and f3 opens photo mode. use the fullscreen option in settings to enter or leave fullscreen. click the canvas or press a key if the browser needs permission to start audio or capture the mouse.

use the game's settings for bindings, graphics, resolution, and aspect ratio. adaptive sizing follows the window; fixed aspect modes use the renderer's projection and bars. the canvas follows pixel density up to a 3840×2160 budget (swapped in portrait) and the device limit. internal resolution and render scale still affect performance.

hidden tabs pause the game and audio. the existing touch controls are included and appear after touch input. settings has one fullscreen toggle that follows the browser's current state. vsync, monitor refresh rate, and gyro controls are omitted from the browser settings.

## texture packs

open settings → graphics → texture packs and choose a zip. the game installs, selects, and applies it immediately. cancel the dialog to use the installed-pack list, where you can switch packs or deactivate the current one. a controller action may need an extra enter press or click to open the browser dialog.

dolphin-style `tex1_` png and dds files, including mip files, are supported. wrapper folders are handled automatically. keep filenames unchanged and include only one variant if a download contains duplicate texture names.

use a regular stored or deflated zip. rar, encrypted, split, and zip64 archives are unsupported. there is no fixed archive, extracted pack, or installed library size limit. individual textures are limited to 64 mib per file and 8192 pixels per image side with at most 16,777,216 pixels per image. available browser memory and storage determine how large a pack can be.

texture files stay on disk in the browser's private filesystem (opfs). imports stream through a worker, and files load only when needed. a 32 mib cache holds recently used files; larger textures are staged temporarily and released after upload. the game displays the original texture until its replacement is ready. missing or unreadable replacements fall back to the original textures.

existing browser packs migrate automatically. saves and settings keep using indexeddb. failed imports keep the active pack intact. opfs requires a secure origin; localhost works with the included server or an ordinary static server, and hosted builds use https. single-threaded builds still need no isolation headers.

hd model actions also open a zip dialog, but use the native converter and require a restart.

## saves

save normally in game, then open f1 → save data → export save. this downloads `nectar-save-backup.json` containing your memory cards and settings. the backup excludes unsaved progress and texture packs.

restore save backup validates the file and asks for confirmation, then restarts to restore it before the engine opens the memory cards. keep your original pack downloads separately.

startup or graphics failures show an error screen with a report download. file-import errors appear in the game menu. reports contain logs and runtime status, without saves or game data.
