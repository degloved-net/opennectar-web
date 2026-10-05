(() => {
    class PackArchive {
        static maxFileBytes = 64 * 1024 * 1024;
        static checksumTable = Uint32Array.from({length: 256}, (_, value) => {
            for (let bit = 0; bit < 8; bit++) {
                value = value & 1 ? 0xedb88320 ^ (value >>> 1) : value >>> 1;
            }
            return value >>> 0;
        });

        constructor(file, entries, centralOffset) {
            this.file = file;
            this.entries = entries;
            this.centralOffset = centralOffset;
        }

        static check(condition, message = 'the zip is damaged or uses an unsupported format') {
            if (!condition) {
                throw new Error(message);
            }
        }

        static safePath(name) {
            return typeof name === 'string' && name.length > 0 && name.length <= 1024
                && !/[\\\x00-\x1f:]/.test(name)
                && !name.replace(/\/$/, '').split('/').some(part => !part || part === '.' || part === '..');
        }

        static async open(file) {
            this.check(file.size >= 22);
            const tail = new DataView(await file.slice(Math.max(0, file.size - 65557)).arrayBuffer());
            let end = -1;
            for (let offset = tail.byteLength - 22; offset >= 0; offset--) {
                if (tail.getUint32(offset, true) === 0x06054b50
                    && offset + 22 + tail.getUint16(offset + 20, true) === tail.byteLength) {
                    end = offset;
                    break;
                }
            }
            this.check(end >= 0);
            const count = tail.getUint16(end + 10, true);
            const centralSize = tail.getUint32(end + 12, true);
            const centralOffset = tail.getUint32(end + 16, true);
            this.check(tail.getUint16(end + 4, true) === 0 && tail.getUint16(end + 6, true) === 0
                && count > 0 && count <= 20000 && count === tail.getUint16(end + 8, true)
                && centralSize <= 16 * 1024 * 1024
                && centralOffset + centralSize === file.size - tail.byteLength + end,
                'split, encrypted, and zip64 archives are not supported; choose a regular zip instead');
            const central = await file.slice(centralOffset, centralOffset + centralSize).arrayBuffer();
            const data = new DataView(central);
            const entries = [];
            const names = new Set();
            let offset = 0;
            for (let index = 0; index < count; index++) {
                this.check(offset + 46 <= centralSize && data.getUint32(offset, true) === 0x02014b50);
                const flags = data.getUint16(offset + 8, true);
                const method = data.getUint16(offset + 10, true);
                const crc = data.getUint32(offset + 16, true);
                const compressed = data.getUint32(offset + 20, true);
                const size = data.getUint32(offset + 24, true);
                const nameLength = data.getUint16(offset + 28, true);
                const next = offset + 46 + nameLength + data.getUint16(offset + 30, true) + data.getUint16(offset + 32, true);
                const mode = data.getUint32(offset + 38, true) >>> 16;
                const localOffset = data.getUint32(offset + 42, true);
                this.check(next <= centralSize && nameLength > 0 && nameLength <= 1024
                    && !(flags & 0x41) && [0, 8].includes(method) && (mode & 0xf000) !== 0xa000
                    && data.getUint16(offset + 34, true) === 0 && localOffset + 30 <= centralOffset
                    && compressed < 0xffffffff && size <= this.maxFileBytes);
                const nameBytes = new Uint8Array(central.slice(offset + 46, offset + 46 + nameLength));
                const name = new TextDecoder('utf-8', {fatal: !!(flags & 0x800)}).decode(nameBytes);
                this.check(this.safePath(name) && !names.has(name.toLowerCase()), 'the zip contains an unsafe or duplicate path');
                names.add(name.toLowerCase());
                entries.push({name, nameBytes, size, compressed, crc, flags, method, localOffset});
                offset = next;
            }
            this.check(offset === centralSize);
            return new PackArchive(file, entries, centralOffset);
        }

        async consume(entry, consume) {
            const header = new DataView(await this.file.slice(entry.localOffset, entry.localOffset + 30).arrayBuffer());
            const nameLength = header.getUint16(26, true);
            const dataOffset = entry.localOffset + 30 + nameLength + header.getUint16(28, true);
            PackArchive.check(header.getUint32(0, true) === 0x04034b50
                && header.getUint16(6, true) === entry.flags && header.getUint16(8, true) === entry.method
                && nameLength === entry.nameBytes.length && dataOffset + entry.compressed <= this.centralOffset);
            const name = new Uint8Array(await this.file.slice(entry.localOffset + 30, entry.localOffset + 30 + nameLength).arrayBuffer());
            PackArchive.check(name.every((value, index) => value === entry.nameBytes[index]));
            if (!(entry.flags & 8)) {
                PackArchive.check(header.getUint32(14, true) === entry.crc
                    && header.getUint32(18, true) === entry.compressed && header.getUint32(22, true) === entry.size);
            }
            let stream = this.file.slice(dataOffset, dataOffset + entry.compressed).stream();
            if (entry.method === 8) {
                try {
                    stream = stream.pipeThrough(new DecompressionStream('deflate-raw'));
                } catch {
                    throw new Error('this browser cannot unpack this zip; use a browser with deflate-raw zip support');
                }
            }
            const reader = stream.getReader();
            let offset = 0;
            let checksum = 0xffffffff;
            try {
                while (true) {
                    const {done, value} = await reader.read();
                    if (done) {
                        break;
                    }
                    PackArchive.check(offset + value.length <= entry.size, 'the zip expands beyond its declared size');
                    offset += value.length;
                    for (const byte of value) {
                        checksum = PackArchive.checksumTable[(checksum ^ byte) & 255] ^ (checksum >>> 8);
                    }
                    await consume(value);
                }
                PackArchive.check(offset === entry.size && ((checksum ^ 0xffffffff) >>> 0) === entry.crc,
                    'the zip contains a damaged texture');
            } finally {
                await reader.cancel().catch(() => {});
                reader.releaseLock();
            }
        }

        async read(entry) {
            const bytes = new Uint8Array(entry.size);
            let offset = 0;
            await this.consume(entry, chunk => { bytes.set(chunk, offset); offset += chunk.length; });
            return bytes;
        }
    }

    class TextureLibrary {
        static root = '/texture-packs';
        static cacheBytes = 32 * 1024 * 1024;

        constructor(runtime) {
            if (runtime.nectarTextureLibrary) return runtime.nectarTextureLibrary;
            runtime.nectarTextureLibrary = this;
            this.runtime = runtime;
            this.fs = runtime.FS;
            this.packs = [];
            this.groups = new Map();
            this.cache = new Map();
            this.residentBytes = 0;
            this.queue = [];
            this.staged = null;
            this.pending = new Set();
            this.failed = new Set();
            this.epoch = 0;
            this.active = '';
            this.serial = 0;
            this.requests = new Map();
            runtime.nectarTextureActivate = id => this.activate(id);
            runtime.nectarTextureRequest = name => this.request(name);
            runtime.nectarTextureConsumed = name => this.consumed(name);
            runtime.nectarTextureSnapshot = () => ({packs: this.packs.length, cachedGroups: this.cache.size,
                cachedBytes: this.residentBytes, cacheBudget: TextureLibrary.cacheBytes,
                pendingGroups: this.pending.size, active: this.active});
        }

        rpc(operation, args = {}) {
            const id = ++this.serial;
            return new Promise((resolve, reject) => {
                this.requests.set(id, {resolve, reject});
                try { this.worker.postMessage({id, operation, ...args}); }
                catch (error) { this.requests.delete(id); reject(error); }
            });
        }

        initialize() {
            if (this.initialization) return this.initialization;
            this.initialization = this.start();
            return this.initialization;
        }

        async start() {
            this.worker = new Worker('nectar-pack-worker.js');
            this.worker.onmessage = ({data}) => {
                if (data.progress !== undefined) { this.progress?.(data.progress, data.total); return; }
                const request = this.requests.get(data.id);
                if (!request) return;
                this.requests.delete(data.id);
                if (data.error) request.reject(new Error(data.error)); else request.resolve(data.result);
            };
            this.worker.onerror = event => {
                for (const request of this.requests.values()) request.reject(new Error(event.message || 'texture worker stopped'));
                this.requests.clear();
            };
            const result = await this.rpc('initialize', {game: Nectar.game});
            this.packs = result.packs;
            this.fs.mkdirTree(TextureLibrary.root);
            const legacy = '/persistent/Load/Textures';
            if (!this.fs.analyzePath(legacy).exists) this.fs.symlink(TextureLibrary.root, legacy);
            for (const pack of this.packs) this.mount(pack);
            for (const warning of result.warnings) this.runtime.nectarRecord?.('textures', warning);
        }

        static textureKey(name) { return name.replace(/\.(png|dds)$/, '').replace(/_mip\d+$/, '').replace('_arb', ''); }

        static groupFiles(files) {
            const groups = new Map();
            for (const file of files) {
                const key = this.textureKey(file.name);
                if (!groups.has(key)) groups.set(key, []);
                groups.get(key).push(file);
            }
            for (const [key, candidates] of groups) {
                const base = candidates.find(file => file.name.endsWith('.dds') && !/_mip\d+\./.test(file.name))
                    || candidates.find(file => !/_mip\d+\./.test(file.name));
                if (!base) { groups.delete(key); continue; }
                const chosen = [base];
                const extension = base.name.split('.').at(-1);
                for (let level = 1; level <= 13; ++level) {
                    const mip = candidates.find(file => file.name.endsWith(`_mip${level}.${extension}`));
                    if (!mip || mip.width !== Math.max(1, base.width >> level)
                        || mip.height !== Math.max(1, base.height >> level)) break;
                    chosen.push(mip);
                    if (mip.width === 1 && mip.height === 1) break;
                }
                groups.set(key, chosen);
            }
            return groups;
        }

        mount(pack) {
            const directory = TextureLibrary.root + '/' + pack.id;
            this.fs.mkdirTree(directory);
            for (const files of TextureLibrary.groupFiles(pack.entries).values()) {
                for (const file of files) this.fs.writeFile(directory + '/' + file.name, new Uint8Array(0));
            }
            this.fs.writeFile(directory + '/pack.json', JSON.stringify(pack));
        }

        activate(id) {
            if (this.active === id) return;
            ++this.epoch;
            for (const name of this.cache.keys()) this.evict(name);
            this.queue = [];
            this.pending.clear();
            this.staged = null;
            this.failed.clear();
            this.active = id;
            this.groups = TextureLibrary.groupFiles(this.packs.find(pack => pack.id === id)?.entries || []);
        }

        evict(name) {
            const cached = this.cache.get(name);
            if (!cached) return;
            for (const file of cached.files) this.fs.writeFile(cached.directory + '/' + file.name, new Uint8Array(0));
            this.residentBytes -= cached.bytes;
            this.cache.delete(name);
        }

        request(name) {
            if (!this.groups.has(name) || this.failed.has(name)) return false;
            if (this.cache.has(name)) {
                const cached = this.cache.get(name);
                this.cache.delete(name); this.cache.set(name, cached);
                return true;
            }
            if (!this.pending.has(name)) {
                this.pending.add(name);
                this.queue.push({name, epoch: this.epoch, pack: this.active, files: this.groups.get(name)});
                this.pump();
            }
            return false;
        }

        async pump() {
            if (this.reading || this.staged || !this.queue.length) return;
            this.reading = true;
            const job = this.queue.shift();
            try {
                const bytes = job.files.reduce((sum, file) => sum + file.size, 0);
                for (const name of this.cache.keys()) {
                    if (this.residentBytes + bytes <= TextureLibrary.cacheBytes) break;
                    this.evict(name);
                }
                const files = await this.rpc('read', {pack: job.pack, names: job.files.map(file => file.name)});
                if (job.epoch !== this.epoch) return;
                const directory = TextureLibrary.root + '/' + job.pack;
                for (const file of files) this.fs.writeFile(directory + '/' + file.name, new Uint8Array(file.bytes), {canOwn: true});
                this.cache.set(job.name, {files, directory, bytes});
                this.residentBytes += bytes;
                this.staged = job.name;
                this.runtime.ccall('nectarTextureReady', null, ['string'], [job.name]);
            } catch (error) {
                if (job.epoch === this.epoch) {
                    this.evict(job.name);
                    if (this.staged === job.name) this.staged = null;
                    this.failed.add(job.name);
                    this.runtime.nectarRecord?.('textures', String(error.message || error));
                }
            } finally {
                if (job.epoch === this.epoch) this.pending.delete(job.name);
                this.reading = false;
                this.pump();
            }
        }

        consumed(name) {
            if (this.residentBytes > TextureLibrary.cacheBytes) this.evict(name);
            if (this.staged === name) { this.staged = null; this.pump(); }
        }

        static plan(entries) {
            const names = new Set();
            const files = [];
            let total = 0;
            for (const entry of entries) {
                const name = entry.name.split('/').at(-1).toLowerCase();
                if (!/^tex1_[a-z0-9_-]+\.(png|dds)$/.test(name) || entry.name.split('/').includes('__MACOSX')) {
                    continue;
                }
                if (names.has(name)) {
                    throw new Error('the pack has duplicate texture names; choose a zip containing only one texture variant');
                }
                if (!Number.isSafeInteger(entry.size) || entry.size <= 0 || entry.size > PackArchive.maxFileBytes) {
                    throw new Error('a texture exceeds the 64 mib file limit');
                }
                names.add(name);
                total += entry.size;
                if (names.size > 20000) {
                    throw new Error('the pack contains too many textures');
                }
                files.push({entry, name});
            }
            if (!files.length) {
                throw new Error('no dolphin-style tex1_ png or dds textures were found');
            }
            return {files, bytes: total};
        }

        static validateTexture(name, bytes) {
            const data = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
            let width;
            let height;
            if (name.endsWith('.png')) {
                PackArchive.check(bytes.length >= 33 && data.getUint32(0) === 0x89504e47
                    && data.getUint32(4) === 0x0d0a1a0a && data.getUint32(8) === 13 && data.getUint32(12) === 0x49484452,
                    'a png texture has an invalid header');
                width = data.getUint32(16);
                height = data.getUint32(20);
            } else {
                PackArchive.check(bytes.length >= 128 && data.getUint32(0, true) === 0x20534444
                    && data.getUint32(4, true) === 124 && data.getUint32(76, true) === 32,
                    'a dds texture has an invalid header');
                width = data.getUint32(16, true);
                height = data.getUint32(12, true);
            }
            PackArchive.check(width > 0 && height > 0 && width <= 8192 && height <= 8192
                && width * height <= 16777216, 'a texture is too large for the browser memory budget');
            return {width, height};
        }

        selected() {
            return this.runtime.ccall('nectarGetTexturePack', 'string', [], []);
        }

        list() {
            return this.packs.slice().sort((left, right) => left.name.localeCompare(right.name));
        }

        async select(id) {
            if (id) await this.initialize();
            if (id && (!/^[a-zA-Z0-9_-]+$/.test(id)
                || !this.fs.analyzePath(TextureLibrary.root + '/' + id).exists)) {
                throw new Error('that texture pack is not installed');
            }
            if (typeof this.runtime._nectarApplyTexturePack !== 'function') {
                throw new Error('the runtime files do not match. install the complete update.');
            }
            const previous = this.selected();
            try {
                this.runtime.ccall('nectarSelectTexturePack', null, ['string'], [id]);
                await this.runtime.nectarSyncSaves();
                if (!this.runtime._nectarApplyTexturePack()) throw new Error('the engine could not load this texture pack');
            } catch (error) {
                this.runtime.ccall('nectarSelectTexturePack', null, ['string'], [previous]);
                this.runtime._nectarApplyTexturePack();
                await this.runtime.nectarSyncSaves().catch(() => {});
                throw error;
            }
        }

        async installZip(file, progress = () => {}) {
            await this.initialize();
            this.progress = progress;
            try {
                const pack = await this.rpc('install', {file});
                this.packs.push(pack);
                this.mount(pack);
                return pack;
            } finally { this.progress = null; }
        }
    }

    globalThis.Nectar = {PackArchive, TextureLibrary};
})();
