importScripts('nectar-packs.js');

const request = item => new Promise((resolve, reject) => {
    item.onsuccess = () => resolve(item.result);
    item.onerror = () => reject(item.error);
});
const complete = transaction => new Promise((resolve, reject) => {
    transaction.oncomplete = resolve;
    transaction.onabort = transaction.onerror = () => reject(transaction.error || new Error('texture storage failed'));
});
let root, database, game;
const packs = new Map();
const indexes = new Map();
let mutation = Promise.resolve();

async function publish(pack) {
    const transaction = database.transaction('packs', 'readwrite');
    const done = complete(transaction);
    transaction.objectStore('packs').put(pack);
    await done;
    packs.set(pack.id, pack);
    indexes.set(pack.id, new Map(pack.entries.map(file => [file.name, file])));
}

async function writer(directory, name, consume) {
    const file = await directory.getFileHandle(name, {create: true});
    let handle, stream;
    try {
        if (file.createSyncAccessHandle) {
            handle = await file.createSyncAccessHandle();
            handle.truncate(0);
            let offset = 0;
            await consume(chunk => {
                let written = 0;
                while (written < chunk.length) {
                    const count = handle.write(chunk.subarray(written), {at: offset + written});
                    if (!count) throw new Error('could not write the texture file');
                    written += count;
                }
                offset += written;
            });
            handle.flush();
        } else {
            stream = await file.createWritable();
            await consume(chunk => stream.write(chunk));
            await stream.close();
            stream = null;
        }
    } finally {
        handle?.close();
        if (stream) await stream.abort().catch(() => {});
    }
}

async function install(file, id) {
    const archive = await Nectar.PackArchive.open(file);
    const plan = Nectar.TextureLibrary.plan(archive.entries);
    const slug = file.name.replace(/\.zip$/i, '').toLowerCase().replace(/[^a-z0-9]+/g, '-').slice(0, 48) || 'pack';
    const pack = {version: 2, id: 'web-' + slug + '-' + crypto.randomUUID().slice(0, 8),
        directory: crypto.randomUUID(), name: file.name.replace(/\.zip$/i, '').slice(0, 100),
        bytes: plan.bytes, files: plan.files.length, entries: []};
    const directory = await root.getDirectoryHandle(pack.directory, {create: true});
    try {
        for (const {entry, name} of plan.files) {
            const header = new Uint8Array(Math.min(128, entry.size));
            let captured = 0;
            await writer(directory, name, write => archive.consume(entry, async chunk => {
                const take = Math.min(header.length - captured, chunk.length);
                if (take) { header.set(chunk.subarray(0, take), captured); captured += take; }
                await write(chunk);
            }));
            const dimensions = Nectar.TextureLibrary.validateTexture(name, header);
            pack.entries.push({name, size: entry.size, ...dimensions});
            postMessage({id, progress: pack.entries.length, total: plan.files.length});
        }
        await publish(pack);
        return pack;
    } catch (error) {
        await root.removeEntry(pack.directory, {recursive: true}).catch(() => {});
        throw error;
    }
}

async function migrateLegacy(warnings) {
    const item = indexedDB.open(game === 'pikmin2' ? 'nectar-pikmin2:/persistent' : '/persistent');
    const legacy = await request(item);
    try {
        if (!legacy.objectStoreNames.contains('FILE_DATA')) return;
        const store = () => legacy.transaction('FILE_DATA', 'readonly').objectStore('FILE_DATA');
        const keys = await request(store().getAllKeys());
        const prefix = '/persistent/Load/Textures/';
        const groups = new Map();
        for (const key of keys) {
            if (!key.startsWith(prefix)) continue;
            const id = key.slice(prefix.length).split('/')[0];
            if (!/^(web-[a-z0-9-]+|GP[IV][A-Z0-9]*|all)$/i.test(id)) continue;
            if (!groups.has(id)) groups.set(id, []);
            groups.get(id).push(key);
        }
        for (const [id, paths] of groups) {
            const pack = {version: 2, id, directory: crypto.randomUUID(), name: id, bytes: 0, files: 0, entries: []};
            const directory = await root.getDirectoryHandle(pack.directory, {create: true});
            try {
                const names = new Set();
                for (const key of paths) {
                    const name = key.split('/').at(-1).toLowerCase();
                    if (name !== 'pack.json' && !/^tex1_[a-z0-9_-]+\.(png|dds)$/.test(name)) continue;
                    const record = await request(store().get(key));
                    if (!record?.contents) continue;
                    if (name === 'pack.json') {
                        try { pack.name = String(JSON.parse(new TextDecoder().decode(record.contents)).name || id).slice(0, 100); }
                        catch {}
                        continue;
                    }
                    if (names.has(name)) throw new Error('the previous pack has duplicate texture names');
                    names.add(name);
                    const bytes = record.contents;
                    if (bytes.length > Nectar.PackArchive.maxFileBytes) throw new Error('a previous texture exceeds the file limit');
                    const dimensions = Nectar.TextureLibrary.validateTexture(name, bytes.subarray(0, 128));
                    await writer(directory, name, write => write(bytes));
                    pack.entries.push({name, size: bytes.length, ...dimensions});
                    pack.bytes += bytes.length;
                }
                if (!pack.entries.length) throw new Error('the previous pack contains no usable textures');
                pack.files = pack.entries.length;
                const previous = packs.get(id);
                await publish(pack);
                const transaction = legacy.transaction('FILE_DATA', 'readwrite');
                const done = complete(transaction);
                for (const key of paths) transaction.objectStore('FILE_DATA').delete(key);
                await done;
                if (previous) await root.removeEntry(previous.directory, {recursive: true}).catch(() => {});
            } catch (error) {
                if (packs.get(id)?.directory !== pack.directory)
                    await root.removeEntry(pack.directory, {recursive: true}).catch(() => {});
                warnings.push('could not migrate ' + id + ': ' + (error.message || error));
            }
        }
    } finally { legacy.close(); }
}

async function initialize(selectedGame) {
    game = selectedGame;
    if (!navigator.storage?.getDirectory) throw new Error('this browser does not support disk storage for texture packs');
    root = await (await navigator.storage.getDirectory()).getDirectoryHandle(game === 'pikmin2' ? 'nectar-pikmin2-textures' : 'nectar-textures', {create: true});
    const item = indexedDB.open(game === 'pikmin2' ? 'nectar-pikmin2-texture-packs' : 'nectar-texture-packs', 1);
    item.onupgradeneeded = () => item.result.createObjectStore('packs', {keyPath: 'id'});
    database = await request(item);
    for (const pack of await request(database.transaction('packs', 'readonly').objectStore('packs').getAll())) {
        packs.set(pack.id, pack);
        indexes.set(pack.id, new Map(pack.entries.map(file => [file.name, file])));
    }
    const warnings = [];
    await migrateLegacy(warnings);
    const used = new Set(Array.from(packs.values(), pack => pack.directory));
    if (navigator.locks) for await (const [name] of root.entries()) {
        if (!used.has(name)) await root.removeEntry(name, {recursive: true});
    }
    return {packs: Array.from(packs.values()), warnings};
}

async function read(packId, names) {
    const pack = packs.get(packId);
    if (!pack) throw new Error('the texture pack is missing');
    const directory = await root.getDirectoryHandle(pack.directory);
    const files = [];
    for (const name of names) {
        const entry = indexes.get(packId).get(name);
        if (!entry) throw new Error('the texture is missing from the pack index');
        const file = await directory.getFileHandle(name);
        let handle;
        if (file.createSyncAccessHandle) {
            try { handle = await file.createSyncAccessHandle({mode: 'read-only'}); }
            catch (error) {
                if (!['TypeError', 'NoModificationAllowedError'].includes(error.name)) throw error;
            }
        }
        if (handle) {
            try {
                if (handle.getSize() !== entry.size) throw new Error('the stored texture is incomplete');
                const bytes = new Uint8Array(entry.size);
                let offset = 0;
                while (offset < bytes.length) {
                    const count = handle.read(bytes.subarray(offset), {at: offset});
                    if (!count) throw new Error('could not read the texture file');
                    offset += count;
                }
                files.push({name, bytes: bytes.buffer});
            } finally { handle.close(); }
        } else {
            const contents = await file.getFile();
            if (contents.size !== entry.size) throw new Error('the stored texture is incomplete');
            files.push({name, bytes: await contents.arrayBuffer()});
        }
    }
    return files;
}

self.onmessage = ({data}) => {
    const run = async () => {
        try {
            let result;
            if (data.operation === 'initialize') result = await initialize(data.game);
            else if (data.operation === 'install') result = await install(data.file, data.id);
            else if (data.operation === 'read') result = await read(data.pack, data.names);
            else throw new Error('unknown texture storage operation');
            postMessage({id: data.id, result}, data.operation === 'read' ? result.map(file => file.bytes) : []);
        } catch (error) {
            postMessage({id: data.id, error: error.name === 'QuotaExceededError'
                ? 'the browser ran out of space for textures' : String(error.message || error)});
        }
    };
    if (data.operation === 'read') run();
    else {
        const locked = () => navigator.locks
            ? navigator.locks.request('nectar-texture-storage', run) : run();
        mutation = mutation.then(locked, locked);
    }
};
