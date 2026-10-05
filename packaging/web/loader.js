(() => {
    async function downloadData(response, expectedSize, progress) {
        if (!response.ok || !Number.isSafeInteger(expectedSize) || expectedSize <= 0 || expectedSize > 1073741824) {
            throw new Error('the game data response or package size is invalid');
        }
        if (!response.body || !progress) {
            const buffer = await response.arrayBuffer();
            if (buffer.byteLength !== expectedSize) {
                throw new Error('the game data download is incomplete');
            }
            progress?.(expectedSize, expectedSize);
            return buffer;
        }
        const bytes = new Uint8Array(expectedSize);
        const reader = response.body.getReader();
        let offset = 0;
        try {
            while (true) {
                const {done, value} = await reader.read();
                if (done) {
                    break;
                }
                if (offset + value.length > expectedSize) {
                    throw new Error('the game data does not match its manifest');
                }
                bytes.set(value, offset);
                offset += value.length;
                progress(offset, expectedSize);
            }
            if (offset !== expectedSize) {
                throw new Error('the game data download is incomplete');
            }
            return bytes.buffer;
        } finally {
            await reader.cancel().catch(() => {});
            reader.releaseLock();
        }
    }

    class BrowserDisplay {
        constructor(runtime, viewport) {
            this.runtime = runtime;
            this.viewport = viewport;
            this.limit = 4096;
            this.pending = false;
            this.size = null;
            this.observer = new ResizeObserver(() => this.schedule());
            this.observer.observe(viewport);
            window.addEventListener('resize', () => this.schedule());
            document.addEventListener('fullscreenchange', () => this.schedule());
            window.visualViewport?.addEventListener('resize', () => this.schedule());
            this.watchDensity();
        }

        watchDensity() {
            this.densityQuery = matchMedia(`(resolution: ${devicePixelRatio}dppx)`);
            this.densityQuery.addEventListener('change', () => {
                this.schedule();
                this.watchDensity();
            }, {once: true});
        }

        schedule() {
            if (this.pending) return;
            this.pending = true;
            requestAnimationFrame(() => {
                this.pending = false;
                this.resize();
            });
        }

        resize(force = false) {
            const {width, height} = this.viewport.getBoundingClientRect();
            if (width <= 0 || height <= 0) return;
            const [maxWidth, maxHeight] = height > width ? [2160, 3840] : [3840, 2160];
            const scale = Math.min(devicePixelRatio, 3, maxWidth / width, maxHeight / height,
                this.limit / width, this.limit / height);
            const next = {width: Math.max(1, Math.round(width * scale)), height: Math.max(1, Math.round(height * scale))};
            if (!force && this.size?.width === next.width && this.size?.height === next.height) return;
            this.size = next;
            if (this.runtime._nectarResize) {
                this.runtime._nectarResize(next.width, next.height);
            } else {
                this.runtime.canvas.width = next.width;
                this.runtime.canvas.height = next.height;
            }
        }
    }

    class BrowserRuntime {
        constructor() {
            this.ui = Object.fromEntries(Array.from(document.querySelectorAll('[id]'), node => [node.id, node]));
            this.logs = [];
            this.ready = false;
            this.failed = false;
            this.storage = 'not mounted';
            this.phase = 'loading';
            this.module = globalThis.Module = this.createRuntime();
            this.display = new BrowserDisplay(this.module, this.ui.viewport);
            this.bindEvents();
            this.display.resize();
            this.start();
        }

        createRuntime() {
            return {
                canvas: this.ui.canvas,
                nectarRecord: (level, text) => this.record(level, text),
                nectarStorage: (state, error) => this.storageChanged(state, error),
                nectarConsumePendingSave: () => Nectar.SaveTransfer.clear(),
                nectarEngineReady: () => queueMicrotask(() => this.engineReady()),
                nectarSetFullscreen: enabled => this.setFullscreen(enabled),
                nectarRequestFile: action => this.files?.request(action),
                nectarChooseTexturePack: id => this.files?.selectTexture(id),
                nectarRestart: () => this.restart(),
                nectarPrepareTextures: fs => {
                    this.module.FS = fs;
                    const library = new Nectar.TextureLibrary(this.module);
                    return library.initialize().catch(error => {
                        library.error = String(error.message || error);
                        this.record('textures', library.error);
                    });
                },
                print: text => {
                    this.record('stdout', text);
                    if (String(text).toLowerCase().includes('[pc port fatal error]')) this.fail(text);
                },
                printErr: text => this.record('stderr', text),
                setStatus: text => this.setStatus(text),
                onRuntimeInitialized: () => {
                    this.phase = 'runtime ready';
                    this.display.resize(true);
                    this.setStatus('starting the engine...');
                },
                onAbort: reason => this.fail('the runtime stopped: ' + reason),
                onExit: code => this.fail('the game closed. reload to play again. exit code: ' + code)
            };
        }

        bindEvents() {
            this.ui.canvas.addEventListener('contextmenu', event => event.preventDefault());
            this.ui.canvas.addEventListener('pointerdown', () => {
                this.ui.canvas.focus({preventScroll: true});
                this.module.nectarResumeAudio?.();
            });
            window.addEventListener('keydown', event => {
                if (['F1', 'F2', 'F3'].includes(event.code)) event.preventDefault();
                this.module.nectarResumeAudio?.();
            }, true);
            this.ui.canvas.addEventListener('webglcontextlost', event => {
                event.preventDefault();
                this.fail('the graphics context was lost. save the error report, then reload.');
            });
            window.addEventListener('error', event => this.fail(event.error?.stack || event.message));
            window.addEventListener('unhandledrejection', event => this.fail(event.reason?.stack || event.reason));
            document.addEventListener('visibilitychange', () => {
                this.updatePause();
                if (document.hidden && this.ready) {
                    this.module.nectarSyncSaves?.().catch(error => this.storageChanged('unavailable', error));
                }
            });
            window.addEventListener('beforeunload', event => {
                if (this.files?.busy && !this.files.restarting) {
                    event.preventDefault();
                    event.returnValue = '';
                }
            });
            window.addEventListener('pagehide', () => this.releaseLock?.());
            window.addEventListener('pageshow', event => {
                if (event.persisted) this.fail('reload before continuing after returning through browser history.');
            });
            this.ui.report.addEventListener('click', () => Nectar.GameFiles.download(
                JSON.stringify(this.report(), null, 2), 'nectar-browser-report.json', 'application/json'));
        }

        preflight() {
            if (this.threaded && (!globalThis.isSecureContext || !globalThis.crossOriginIsolated)) {
                throw new Error('cross-origin isolation is required. run serve.py and open the local address, not the html file.');
            }
            if (typeof WebAssembly === 'undefined') {
                throw new Error('this browser does not provide webassembly');
            }
            if (this.threaded && typeof SharedArrayBuffer === 'undefined') {
                throw new Error('this browser does not provide webassembly shared memory; use the single-threaded build');
            }
            const gl = document.createElement('canvas').getContext('webgl2');
            if (!gl) throw new Error('this browser could not create a webgl 2 context');
            this.graphics = {version: gl.getParameter(gl.VERSION), maxTextureSize: gl.getParameter(gl.MAX_TEXTURE_SIZE)};
            this.display.limit = Math.min(4096, this.graphics.maxTextureSize);
            gl.getExtension('WEBGL_lose_context')?.loseContext();
        }

        async claimSession() {
            if (!navigator.locks) {
                this.record('storage', 'tab locking unavailable; keep only one game tab open');
                return;
            }
            await new Promise((resolve, reject) => {
                navigator.locks.request(Nectar.game === 'pikmin2' ? 'nectar-pikmin2-game' : 'open-nectar-game', {ifAvailable: true}, lock => {
                    if (!lock) {
                        reject(new Error('another game tab is using these saves. close it before opening this one.'));
                        return;
                    }
                    resolve();
                    return new Promise(release => { this.releaseLock = release; });
                }).catch(reject);
            });
        }

        loadScript(source) {
            return new Promise((resolve, reject) => {
                const script = document.createElement('script');
                script.src = source;
                script.onload = resolve;
                script.onerror = () => reject(new Error('could not load ' + source + '; keep the update files together'));
                document.body.appendChild(script);
            });
        }

        async loadData(assets) {
            const url = 'nectar.data?v=' + assets.outputs['nectar.data'];
            let cache;
            try {
                cache = await caches.open('nectar-game-data');
                const saved = await cache.match(url);
                if (saved) {
                    this.setStatus('loading cached game data...');
                    const data = await downloadData(saved, assets.bytes);
                    this.record('cache', 'using cached game data');
                    return data;
                }
            } catch (error) {
                this.record('cache', error.message || error);
                await cache?.delete(url).catch(() => {});
            }
            const response = await fetch(url);
            const stored = cache && response.ok ? cache.put(url, response.clone()).then(async () => {
                for (const key of await cache.keys()) {
                    if (key.url !== new URL(url, location.href).href) await cache.delete(key);
                }
            }).catch(error => {
                this.record('cache', 'game data could not be cached: ' + error.message);
            }) : Promise.resolve();
            try {
                let lastPercent = -1;
                this.ui.progress.max = assets.bytes;
                const data = await downloadData(response, assets.bytes, (done, total) => {
                    const percent = Math.floor(done / total * 100);
                    if (percent === lastPercent) return;
                    lastPercent = percent;
                    this.ui.progress.value = done;
                    this.setStatus(`loading game data... ${percent}%`);
                });
                await stored;
                return data;
            } catch (error) {
                await stored;
                await cache?.delete(url).catch(() => {});
                throw error;
            }
        }

        async start() {
            try {
                const buildResponse = await fetch('nectar-build.json', {cache: 'no-cache'});
                if (!buildResponse.ok) throw new Error('the build manifest is missing; keep the runtime files together');
                const build = await buildResponse.json();
                Nectar.game = build.game === 'pikmin2' ? 'pikmin2' : 'pikmin1';
                if (Nectar.game === 'pikmin2') {
                    document.title = 'open nectar · pikmin 2';
                    this.ui['source-chooser'].querySelector('.source-formats').textContent =
                        'gamecube · iso/gcm, rvz, wia, gcz, ciso, tgc, wbfs · pikmin 2 usa or europe';
                    this.module.preRun ||= [];
                    this.module.preRun.unshift(() => {
                        const idb = this.module.FS.filesystems.IDBFS;
                        const getDB = idb.getDB;
                        idb.getDB = (name, callback) => getDB.call(idb, 'nectar-pikmin2:' + name, callback);
                    });
                }
                this.threaded = build.threaded === undefined ? true : build.threaded;
                if (typeof this.threaded !== 'boolean') throw new Error('the build threading mode is invalid');
                this.preflight();
                await this.claimSession();
                let pending;
                try {
                    pending = await Nectar.SaveTransfer.read();
                } catch (error) {
                    this.record('storage', 'save restoration could not be checked: ' + error.message);
                }
                if (pending) this.module.nectarPendingSave = Nectar.SaveArchive.decode(pending);
                const source = this.ui['runtime-script'].content.querySelector('script')?.getAttribute('src');
                if (!source) throw new Error('the generated engine script is missing');
                build.runtimes ||= {[build.discKey || 'GPIE01:1']: source};
                for (const script of Object.values(build.runtimes)) {
                    if (!/^nectar(?:-pal)?\.js$/.test(script)) throw new Error('the engine list is invalid');
                }
                let assets = null;
                const response = await fetch('nectar-assets.json', {cache: 'no-cache'});
                if (response.ok) {
                    assets = await response.json();
                    if (assets.version !== 1 || !Number.isSafeInteger(assets.bytes) || assets.bytes < 1
                        || !/^[a-f0-9]{64}$/.test(assets.outputs?.['nectar.data'])) {
                        throw new Error('the game asset manifest is invalid');
                    }
                } else if (response.status !== 404) {
                    throw new Error('could not check the packaged game data; reload to try again');
                }
                const choice = await Nectar.Discs.choose(this, build.rom ? {rom: build.rom} : assets, build);
                let engine;
                if (choice.install) {
                    engine = build.runtimes[choice.install.key];
                    this.module.nectarLoadOwnGame = fs => Nectar.Discs.load(choice.install, fs, this);
                } else {
                    assets = choice.bundled;
                    engine = build.runtimes[assets.discKey || build.discKey || 'GPIE01:1'];
                    this.preloadedData = await this.loadData(assets);
                    this.module.getPreloadedPackage = (name, size) => {
                        if (!this.preloadedData || this.preloadedData.byteLength !== size) throw new Error('game data does not match its metadata');
                        const buffer = this.preloadedData;
                        this.preloadedData = null;
                        return buffer;
                    };
                    await this.loadScript('nectar-assets.js');
                }
                if (!engine) throw new Error('this web build does not contain an engine matching your game copy');
                await this.loadScript(engine);
            } catch (error) {
                this.fail(error.message || error);
            }
        }

        engineReady() {
            if (this.failed) return;
            this.ready = true;
            this.phase = 'engine loop';
            this.files = new Nectar.GameFiles(this.module, this.ui['file-picker'], (level, text) => this.record(level, text));
            this.display.resize(true);
            this.ui.loading.hidden = true;
            this.ui.canvas.focus({preventScroll: true});
            this.updatePause();
            if (this.storage !== 'ready') this.files.notice('browser storage is unavailable. saves and packs may not persist.', true);
        }

        storageChanged(state, error) {
            this.storage = state;
            this.record('storage', error ? `${state}: ${error}` : state);
            if (state === 'unavailable' && this.ready) this.files.notice('browser storage failed. back up your saves in game settings.', true);
        }

        updatePause() {
            if (!this.ready) return;
            const paused = document.hidden || this.failed;
            this.module._nectarSetPaused(Number(paused));
            this.module.nectarSetAudioPaused?.(paused);
        }

        async setFullscreen(enabled) {
            try {
                if (enabled && !document.fullscreenElement) await document.documentElement.requestFullscreen();
                else if (!enabled && document.fullscreenElement) await document.exitFullscreen();
            } catch (error) {
                this.files?.notice('fullscreen was blocked by the browser. try selecting the fullscreen option again.', true);
                this.record('fullscreen', error.message || error);
            }
        }

        async restart() {
            if (this.files?.busy) return;
            try {
                await this.module.nectarSyncSaves();
                location.reload();
            } catch (error) {
                this.files?.notice('restart cancelled: saves could not be written.', true);
                this.record('storage', error.message || error);
            }
        }

        setStatus(text) {
            if (!this.failed && text) {
                this.ui.status.hidden = false;
                this.ui.status.textContent = String(text);
            }
        }

        fail(message) {
            this.failed = true;
            this.phase = 'failed';
            this.record('fatal', message);
            this.ui.status.textContent = String(message);
            this.ui.status.hidden = false;
            this.ui.loading.hidden = false;
            this.ui.progress.hidden = true;
            this.ui.report.hidden = false;
            this.updatePause();
        }

        record(level, message) {
            this.logs.push({time: new Date().toISOString(), level, text: String(message).slice(0, 2048)});
            if (this.logs.length > 512) this.logs.shift();
        }

        report() {
            return {version: 3, phase: this.phase, storage: this.storage, graphics: this.graphics,
                threaded: this.threaded, isolated: globalThis.crossOriginIsolated, userAgent: navigator.userAgent,
                canvas: this.display?.size, runtime: this.ready ? this.module.nectarSnapshot?.() : null,
                logs: this.logs.slice()};
        }
    }

    new BrowserRuntime();
})();
