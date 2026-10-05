(() => {
    const Store = Nectar.DiscStore;

    async function downloadRom(manifest, runtime, signal) {
        const {parts, bytes, name} = manifest;
        if (!Array.isArray(parts) || !parts.length || parts.length > 10000
            || !Number.isSafeInteger(bytes) || bytes <= 0 || typeof name !== 'string'
            || !/\.(iso|gcm|rvz|wia|gcz|ciso|tgc|wbfs)$/i.test(name)) {
            throw new Error('the included rom manifest is invalid');
        }
        const paths = new Set();
        let total = 0;
        for (const part of parts) {
            if (!/^rom\/part-[0-9]{5}\.bin$/.test(part.path) || paths.has(part.path)
                || !Number.isSafeInteger(part.bytes) || part.bytes <= 0 || part.bytes > 20000000
                || !/^[a-f0-9]{64}$/.test(part.sha256)) throw new Error('the included rom part is invalid');
            paths.add(part.path);
            total += part.bytes;
        }
        if (total !== bytes) throw new Error('the included rom size is invalid');
        const controller = new AbortController();
        const abort = () => controller.abort(signal.reason);
        signal.addEventListener('abort', abort, {once: true});
        if (signal.aborted) abort();
        const blobs = new Array(parts.length);
        let next = 0, completed = 0, failure;
        const download = async () => {
            try {
                while (next < parts.length) {
                    controller.signal.throwIfAborted();
                    const index = next++, part = parts[index];
                    const response = await fetch(part.path, {signal: controller.signal});
                    if (!response.ok) throw new Error('could not download the included rom part: ' + part.path);
                    const blob = await response.blob();
                    if (blob.size !== part.bytes) throw new Error('the included rom part is incomplete: ' + part.path);
                    const checksum = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', await blob.arrayBuffer())),
                        byte => byte.toString(16).padStart(2, '0')).join('');
                    if (checksum !== part.sha256) throw new Error('the included rom part is damaged: ' + part.path);
                    controller.signal.throwIfAborted();
                    blobs[index] = blob;
                    completed += blob.size;
                    runtime.ui.progress.max = bytes;
                    runtime.ui.progress.value = completed;
                    runtime.setStatus('downloading rom… ' + Math.floor(completed * 100 / bytes) + '%');
                }
            } catch (error) { failure ||= error; controller.abort(); }
        };
        try { await Promise.all([download(), download()]); }
        finally { signal.removeEventListener('abort', abort); }
        if (failure) throw failure;
        signal.throwIfAborted();
        return new File(blobs, name);
    }

    async function choose(runtime, bundled, build) {
        const ui = runtime.ui;
        let cached = null;
        try {
            const db = await Store.open();
            try {
                cached = await Store.active(db);
                await Store.clean(db, cached?.id);
            } finally { db.close(); }
        } catch (error) { runtime.record('disc storage', error.message || error); }

        ui['source-chooser'].hidden = false;
        ui.progress.hidden = true;
        ui.status.hidden = true;
        ui['source-packaged'].hidden = !bundled;
        ui['source-packaged'].textContent = bundled?.rom ? 'play' : 'play included copy';
        ui['source-cached'].hidden = !cached || !Object.hasOwn(build.runtimes, cached.key);
        let importing = false;
        return new Promise(resolve => {
            const finish = choice => {
                ui['source-chooser'].hidden = true;
                ui.progress.hidden = false;
                runtime.setStatus('loading…');
                resolve(choice);
            };
            ui['source-packaged'].onclick = () => bundled?.rom
                ? importFile(signal => downloadRom(bundled.rom, runtime, signal)) : finish({bundled});
            ui['source-cached'].onclick = () => finish({install: cached});
            ui['source-own'].onclick = () => { ui['game-source-file'].value = ''; ui['game-source-file'].click(); };
            const importFile = async source => {
                if (!source || importing) return;
                importing = true;
                const controller = new AbortController();
                ui['source-error'].textContent = '';
                for (const name of ['source-own', 'source-packaged', 'source-cached']) ui[name].disabled = true;
                ui['source-cancel'].hidden = false;
                ui.progress.hidden = false;
                runtime.setStatus('opening rom…');
                ui.progress.removeAttribute('value');
                ui['source-cancel'].onclick = () => controller.abort(new Error('import canceled. your previous copy is unchanged.'));
                const id = 'disc-' + Array.from(crypto.getRandomValues(new Uint8Array(16)), byte => byte.toString(16).padStart(2, '0')).join('');
                let worker;
                let abort;
                try {
                    const file = typeof source === 'function' ? await source(controller.signal) : source;
                    controller.signal.throwIfAborted();
                    const install = await new Promise((done, reject) => {
                        worker = new Worker('nectar-disc-worker.js');
                        abort = () => reject(controller.signal.reason);
                        controller.signal.addEventListener('abort', abort, {once: true});
                        worker.onerror = () => reject(new Error('the disc importer could not start. keep the decoder files with this web build.'));
                        worker.onmessage = ({data}) => {
                            if (data.type === 'progress') {
                                runtime.setStatus(data.text);
                                if (data.total) { ui.progress.max = data.total; ui.progress.value = data.done; }
                            } else if (data.type === 'ready') {
                                ui['source-cancel'].hidden = true;
                                controller.signal.removeEventListener('abort', abort);
                                done(data.install);
                            } else if (data.type === 'error') reject(new Error(data.text));
                        };
                        worker.postMessage({file, id, supported: build.runtimes, game: build.game});
                    });
                    const db = await Store.open();
                    try {
                        await Store.commit(db, install);
                        await Store.clean(db, id).catch(error => runtime.record('disc storage', error.message));
                    } finally { db.close(); }
                    finish({install});
                } catch (error) {
                    controller.abort();
                    worker?.terminate();
                    try {
                        const db = await Store.open();
                        try { await Store.remove(db, id); } finally { db.close(); }
                    } catch {}
                    ui.status.hidden = true;
                    ui['source-error'].textContent = error.message || String(error);
                    ui.progress.hidden = true;
                } finally {
                    if (abort) controller.signal.removeEventListener('abort', abort);
                    worker?.terminate();
                    importing = false;
                    ui['source-cancel'].hidden = true;
                    for (const name of ['source-own', 'source-packaged', 'source-cached']) ui[name].disabled = false;
                }
            };
            ui['game-source-file'].onchange = () => importFile(ui['game-source-file'].files[0]);
            ui['source-chooser'].ondragover = event => { event.preventDefault(); };
            ui['source-chooser'].ondrop = event => {
                event.preventDefault();
                if (event.dataTransfer.files.length !== 1) { ui['source-error'].textContent = 'drop one game image at a time.'; return; }
                importFile(event.dataTransfer.files[0]);
            };
        });
    }

    async function load(install, fs, runtime) {
        const {files} = install;
        if (!Array.isArray(files) || !files.length || files.length > 65536) throw new Error('the saved game-data manifest is invalid. import your disc again.');
        const paths = new Set();
        let total = 0;
        for (const file of files) {
            if (!Nectar.DiscFiles.safePath(file.path) || paths.has(file.path) || !Number.isSafeInteger(file.size)
                || file.size < 0 || file.size > 256 * 1024 * 1024) throw new Error('the saved game data contains an invalid file.');
            paths.add(file.path);
            total += file.size;
        }
        const marker = ['GPVP01:0', 'GPVE01:0'].includes(install.key) ? 'AudioRes/PSound.aaf' : 'dataDir/parms/gamePrms.bin';
        if (total > Nectar.DiscFiles.maxBytes || !files.some(file => file.path === marker && file.size > 0)) throw new Error('the saved game data is incomplete.');
        const db = await Store.open();
        try {
            let completed = 0;
            for (const file of files) {
                const blob = await Store.read(db, 'files', [install.id, file.path]);
                if (!(blob instanceof Blob) || blob.size !== file.size) throw new Error('saved game data is missing or damaged. import your disc again.');
                const path = '/assets/' + file.path;
                fs.mkdirTree(path.slice(0, path.lastIndexOf('/')));
                fs.writeFile(path, new Uint8Array(await blob.arrayBuffer()), {canOwn: true});
                completed += file.size;
                runtime.ui.progress.max = total;
                runtime.ui.progress.value = completed;
                runtime.setStatus('loading ' + install.description.toLowerCase() + '… ' + Math.floor(completed * 100 / total) + '%');
            }
        } finally { db.close(); }
    }
    Nectar.Discs = {choose, load};
})();
