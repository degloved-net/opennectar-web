(() => {
    const config = Nectar.CdnConfig;
    const request = item => new Promise((resolve, reject) => {
        item.onsuccess = () => resolve(item.result);
        item.onerror = () => reject(item.error);
    });
    const complete = transaction => new Promise((resolve, reject) => {
        transaction.oncomplete = resolve;
        transaction.onabort = transaction.onerror = () => reject(transaction.error || new Error('browser storage failed'));
    });
    class Storage {
        async open() {
            if (!this.database) this.database = new Promise((resolve, reject) => {
                const item = indexedDB.open(config.storage, 1);
                item.onupgradeneeded = () => {
                    for (const name of ['resources', 'packs', 'pack-files', 'installs', 'files', 'transfer'])
                        item.result.createObjectStore(name);
                };
                item.onsuccess = () => resolve(item.result);
                item.onerror = () => reject(item.error);
                item.onblocked = () => reject(new Error('close other launcher tabs before continuing'));
            });
            return this.database;
        }
        async access(name, mode, action) {
            const db = await this.open();
            const transaction = db.transaction(name, mode);
            const done = complete(transaction);
            const item = action(transaction.objectStore(name));
            await done;
            return item?.result;
        }
        get(name, key) { return this.access(name, 'readonly', store => store.get(key)); }
        put(name, key, value) { return this.access(name, 'readwrite', store => store.put(value, key)); }
        async removeRange(name, range) {
            await this.access(name, 'readwrite', store => store.delete(range));
        }
        async chunks(name, prefix, expectedSize) {
            const db = await this.open();
            const transaction = db.transaction(name, 'readonly');
            const done = complete(transaction);
            const blobs = [];
            const range = IDBKeyRange.bound([...prefix, 0], [...prefix, Number.MAX_SAFE_INTEGER]);
            const cursor = transaction.objectStore(name).openCursor(range);
            let invalid = false;
            cursor.onsuccess = () => {
                const item = cursor.result;
                if (!item) return;
                if (item.key.at(-1) !== blobs.length || !(item.value instanceof Blob)) invalid = true;
                blobs.push(item.value); item.continue();
            };
            await done;
            const blob = new Blob(blobs);
            if (invalid || blob.size !== expectedSize) throw new Error('cached file data is incomplete');
            return blob;
        }
        async clean(name, keep) {
            await this.access(name, 'readwrite', store => {
                const cursor = store.openKeyCursor();
                cursor.onsuccess = () => {
                    const item = cursor.result;
                    if (!item) return;
                    if (!keep(item.key)) store.delete(item.key);
                    item.continue();
                };
                return cursor;
            });
        }
    }
    const storage = new Storage();
    const files = config.files;
    const digest = async blob => Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', await blob.arrayBuffer())),
        byte => byte.toString(16).padStart(2, '0')).join('');

    async function resource(name, signal) {
        const file = files[name];
        if (!file) throw new Error('the launcher is missing a resource: ' + name);
        signal?.throwIfAborted();
        const cached = await storage.get('resources', file.sha256);
        if (cached instanceof Blob && cached.size === file.bytes && await digest(cached) === file.sha256) return cached;
        for (let attempt = 0; attempt < 3; ++attempt) {
            signal?.throwIfAborted();
            try {
                const response = await fetch(config.root + '/' + name, {credentials: 'omit', signal});
                if (!response.ok) throw new Error('could not download ' + name + ' (http ' + response.status + ')');
                const blob = await response.blob();
                if (blob.size !== file.bytes || await digest(blob) !== file.sha256)
                    throw new Error('the download does not match this launcher: ' + name);
                signal?.throwIfAborted();
                await storage.put('resources', file.sha256, blob);
                return blob;
            } catch (error) {
                if (signal?.aborted || error.name === 'QuotaExceededError' || attempt === 2) throw error;
                await new Promise(resolve => setTimeout(resolve, 300 * (attempt + 1)));
            }
        }
    }

    async function runWorker(source, message, write, progress = () => {}, signal) {
        signal?.throwIfAborted();
        const url = URL.createObjectURL(new Blob(source, {type: 'text/javascript'}));
        let worker;
        try { worker = new Worker(url); }
        catch (error) { URL.revokeObjectURL(url); throw error; }
        return new Promise((resolve, reject) => {
            let settled = false, writes = Promise.resolve();
            const finish = async (error, result) => {
                if (settled) return;
                settled = true;
                worker.terminate(); URL.revokeObjectURL(url);
                signal?.removeEventListener('abort', abort);
                await writes;
                if (error) reject(error); else resolve(result);
            };
            const abort = () => finish(new Error('import canceled. your previous copy is unchanged.'));
            worker.onmessage = ({data}) => {
                if (settled) return;
                if (data.type === 'store') {
                    writes = writes.then(async () => {
                        try { await write(data); if (!settled) worker.postMessage({request: data.request}); }
                        catch (error) { if (!settled) worker.postMessage({request: data.request, error: String(error.message || error)}); }
                    });
                } else if (data.type === 'progress') progress(data);
                else if (data.type === 'ready') finish(null, data);
                else if (data.type === 'error') finish(new Error(data.text));
            };
            worker.onerror = event => finish(new Error(event.message || 'the decoder worker stopped'));
            signal?.addEventListener('abort', abort, {once: true});
            if (signal?.aborted) { abort(); return; }
            try { worker.postMessage(message, message.wasm ? [message.wasm] : []); }
            catch (error) { finish(error); }
        });
    }

    const BaseLibrary = Nectar.TextureLibrary;
    class IndexedTextureLibrary extends BaseLibrary {
        static get cacheBytes() { return BaseLibrary.cacheBytes; }
        static set cacheBytes(value) { BaseLibrary.cacheBytes = value; }
        mount(pack) {
            super.mount(pack);
            this.indexes ||= new Map();
            this.indexes.set(pack.id, {pack, files: new Map(pack.entries.map(file => [file.name, file]))});
        }
        async start() {
            const packs = await storage.access('packs', 'readonly', store => store.getAll());
            this.packs = packs;
            const directories = new Set(packs.map(pack => pack.directory));
            await storage.clean('pack-files', key => directories.has(key[0]));
            this.fs.mkdirTree(BaseLibrary.root);
            if (!this.fs.analyzePath('/persistent/Load/Textures').exists)
                this.fs.symlink(BaseLibrary.root, '/persistent/Load/Textures');
            for (const pack of packs) this.mount(pack);
        }
        async rpc(operation, args) {
            if (operation === 'read') {
                const index = this.indexes.get(args.pack);
                if (!index) throw new Error('the texture pack is missing');
                const pack = index.pack;
                const output = [];
                for (const name of args.names) {
                    const entry = index.files.get(name);
                    if (!entry) throw new Error('the texture is missing from the pack index');
                    const blob = await storage.chunks('pack-files', [pack.directory, name], entry.size);
                    output.push({name, bytes: await blob.arrayBuffer()});
                }
                return output;
            }
            if (operation !== 'install') throw new Error('unknown texture storage operation');
            const directory = crypto.randomUUID();
            try {
                const result = await runWorker([Nectar.CdnSources.packs, '\n', Nectar.CdnSources.packWorker],
                    {file: args.file, directory}, data => storage.put('pack-files',
                        [directory, data.args.name, data.args.part], data.args.blob),
                    data => this.progress?.(data.done, data.total));
                const slug = args.file.name.replace(/\.zip$/i, '').toLowerCase().replace(/[^a-z0-9]+/g, '-').slice(0, 48) || 'pack';
                const pack = {version: 2, id: 'web-' + slug + '-' + crypto.randomUUID().slice(0, 8), directory,
                    name: args.file.name.replace(/\.zip$/i, '').slice(0, 100), bytes: result.bytes,
                    files: result.entries.length, entries: result.entries};
                await storage.put('packs', pack.id, pack);
                return pack;
            } catch (error) {
                await storage.removeRange('pack-files', IDBKeyRange.bound([directory, ''], [directory, '\uffff'])).catch(() => {});
                throw error;
            }
        }
    }
    Nectar.TextureLibrary = IndexedTextureLibrary;
    Nectar.SaveTransfer.access = (mode, action) => storage.access('transfer', mode, action);
    Nectar.DiscStore.open = async () => {
        await storage.open();
        return request(indexedDB.open(config.storage, 1));
    };
    const originalRead = Nectar.DiscStore.read;
    Nectar.DiscStore.read = async (db, name, key) => {
        if (name !== 'files') return originalRead(db, name, key);
        const install = await originalRead(db, 'installs', key[0]);
        const entry = install?.files.find(file => file.path === key[1]);
        if (!entry) throw new Error('the saved game-data manifest is missing');
        return storage.chunks('files', key, entry.size);
    };

    async function importDisc(file, runtime, build, signal) {
        const id = 'disc-' + crypto.randomUUID();
        try {
            const [script, wasm] = await Promise.all([resource('nectar-disc.js', signal), resource('nectar_disc.wasm', signal)]);
            const result = await runWorker([await script.text(), '\n', Nectar.CdnSources.discFiles, '\n', Nectar.CdnSources.discWorker],
                {file, id, supported: build.runtimes, wasm: await wasm.arrayBuffer()}, data => {
                    if (data.operation === 'install') return storage.put('installs', id, data.args.install);
                    if (data.operation === 'file') return storage.put('files', [id, data.args.path, data.args.part], data.args.blob);
                    throw new Error('unknown disc storage operation');
                }, data => {
                    runtime.setStatus(data.text);
                    runtime.ui.progress.max = data.total;
                    runtime.ui.progress.value = data.done;
                }, signal);
            signal?.throwIfAborted();
            runtime.ui['source-cancel'].hidden = true;
            const db = await storage.open();
            await Nectar.DiscStore.commit(db, result.install);
            await Nectar.DiscStore.clean(db, id).catch(error => runtime.record('disc storage', String(error)));
            return result.install;
        } catch (error) {
            await Nectar.DiscStore.remove(await storage.open(), id).catch(() => {});
            throw error;
        }
    }

    async function downloadRom(runtime, signal) {
        const blobs = new Array(config.rom.parts.length);
        let next = 0, done = 0, failure;
        const controller = new AbortController();
        const abort = () => controller.abort(signal.reason);
        signal.addEventListener('abort', abort, {once: true});
        if (signal.aborted) abort();
        const download = async () => {
            try {
                while (next < blobs.length) {
                    controller.signal.throwIfAborted();
                    const index = next++;
                    blobs[index] = await resource(config.rom.parts[index], controller.signal);
                    done += blobs[index].size;
                    runtime.ui.progress.max = config.rom.bytes;
                    runtime.ui.progress.value = done;
                    runtime.setStatus('downloading rom… ' + Math.floor(done / config.rom.bytes * 100) + '%');
                }
            } catch (error) { failure ||= error; controller.abort(); }
        };
        try { await Promise.all([download(), download()]); }
        finally { signal.removeEventListener('abort', abort); }
        if (failure) throw failure;
        signal.throwIfAborted();
        return new File(blobs, config.rom.name);
    }

    Nectar.Discs.choose = async (runtime, bundled, build) => {
        const ui = runtime.ui;
        const db = await storage.open();
        const cached = await Nectar.DiscStore.active(db);
        await Nectar.DiscStore.clean(db, cached?.id);
        ui['source-chooser'].hidden = false;
        ui.progress.hidden = true; ui.status.hidden = true;
        ui['source-cached'].hidden = !cached || !Object.hasOwn(build.runtimes, cached.key);
        ui['source-packaged'].hidden = !config.rom;
        ui['source-packaged'].textContent = 'play';
        let importing = false;
        return new Promise(resolve => {
            const finish = install => {
                ui['source-chooser'].hidden = true; ui.progress.hidden = false;
                runtime.setStatus('loading…'); resolve({install});
            };
            const run = async getFile => {
                if (importing) return;
                importing = true;
                const controller = new AbortController();
                ui['source-error'].textContent = '';
                for (const name of ['source-own', 'source-cached', 'source-packaged']) ui[name].disabled = true;
                ui['source-cancel'].hidden = false; ui.progress.hidden = false;
                ui.progress.removeAttribute('value'); runtime.setStatus('opening rom…');
                ui['source-cancel'].onclick = () => controller.abort();
                try {
                    const file = await getFile(controller.signal);
                    if (file) finish(await importDisc(file, runtime, build, controller.signal));
                } catch (error) {
                    controller.abort();
                    ui.status.hidden = true; ui.progress.hidden = true;
                    ui['source-error'].textContent = error.name === 'AbortError'
                        ? 'import canceled. your previous copy is unchanged.' : String(error.message || error);
                } finally {
                    importing = false; ui['source-cancel'].hidden = true;
                    for (const name of ['source-own', 'source-cached', 'source-packaged']) ui[name].disabled = false;
                }
            };
            ui['source-cached'].onclick = () => finish(cached);
            ui['source-packaged'].onclick = () => run(signal => downloadRom(runtime, signal));
            ui['source-own'].onclick = () => {ui['game-source-file'].value = ''; ui['game-source-file'].click();};
            ui['game-source-file'].onchange = () => {
                const file = ui['game-source-file'].files[0]; if (file) run(() => file);
            };
            ui['source-chooser'].ondragover = event => event.preventDefault();
            ui['source-chooser'].ondrop = event => {
                event.preventDefault();
                if (event.dataTransfer.files.length !== 1) {ui['source-error'].textContent = 'drop one game image at a time.'; return;}
                run(() => event.dataTransfer.files[0]);
            };
        });
    };

    Nectar.Cdn = {
        config, storage, resource,
        configureRuntime(module) {
            module.locateFile = name => config.root + '/' + name;
            module.preRun = [() => {
                const idb = module.FS.filesystems.IDBFS;
                const getDB = idb.getDB;
                idb.getDB = (name, callback) => getDB(config.storage + '-saves:' + name, callback);
            }];
            return module;
        },
        async loadEngine(runtime, engine) {
            const [script, wasm] = await Promise.all([resource(engine), resource(engine.replace(/\.js$/, '.wasm'))]);
            const binary = await wasm.arrayBuffer();
            runtime.module.instantiateWasm = (imports, receive) => {
                WebAssembly.instantiate(binary, imports).then(result => {
                    receive(result.instance, result.module);
                    delete runtime.module.instantiateWasm;
                })
                    .catch(error => runtime.fail('could not start the engine: ' + String(error.message || error)));
                return {};
            };
            const url = URL.createObjectURL(new Blob([script], {type: 'text/javascript'}));
            try { await runtime.loadScript(url); }
            finally { URL.revokeObjectURL(url); }
        }
    };
})();
