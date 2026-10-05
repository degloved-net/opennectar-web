(() => {
    const maxBytes = 2 * 1024 * 1024 * 1024;
    const supported = {'GPIE01:1': 'pikmin usa rev. 1', 'GPIP01:0': 'pikmin europe', 'GPVP01:0': 'pikmin 2 europe', 'GPVE01:0': 'pikmin 2 usa'};

    function safePath(path) {
        return typeof path === 'string' && path.length < 1024
            && /^(?:(dataDir|MovieData|user|AudioRes|movie|new_screen|System|system|banner|enemy|memoryCard|message|thp)\/|gameConfig\.ini$)/.test(path)
            && path.split('/').every(name => name && name !== '.' && name !== '..' && !/[\x00-\x1f\\:]/.test(name));
    }

    function scan(read, size) {
        const header = read(0, 0x440);
        const view = new DataView(header.buffer, header.byteOffset, header.byteLength);
        if (view.getUint32(0x1c) !== 0xc2339f3d) throw new Error('choose a gamecube pikmin disc image. wii discs are unsupported.');
        const gameId = new TextDecoder().decode(header.subarray(0, 6));
        const revision = header[7];
        const key = gameId + ':' + revision;
        if (!supported[key]) throw new Error('unsupported disc. choose pikmin usa rev. 1, pikmin europe, or pikmin 2 usa or europe.');
        const pikmin2 = gameId === 'GPVP01' || gameId === 'GPVE01';
        const limit = pikmin2 ? maxBytes : maxBytes / 2;
        const offset = view.getUint32(0x424), length = view.getUint32(0x428);
        if (length < 12 || length > 16 * 1024 * 1024 || offset > size || length > size - offset) throw new Error('the disc file table is damaged.');
        const bytes = read(offset, length);
        const fst = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
        const count = fst.getUint32(8);
        if (bytes[0] !== 1 || !count || count > 65536 || count * 12 > length) throw new Error('the disc file table is invalid.');
        const strings = count * 12;
        const stack = [{index: 0, end: count, path: ''}];
        const files = [], paths = new Set();
        let total = 0;
        for (let i = 1; i < count; ++i) {
            while (i >= stack.at(-1).end && stack.length > 1) stack.pop();
            const parent = stack.at(-1);
            const word = fst.getUint32(i * 12), type = word >>> 24;
            if (type > 1) throw new Error('the disc file table has an invalid entry.');
            const begin = strings + (word & 0xffffff);
            const end = bytes.indexOf(0, begin);
            if (begin >= length || end < begin || end - begin > 255) throw new Error('the disc has an invalid filename.');
            const raw = bytes.subarray(begin, end);
            let name;
            try { name = new TextDecoder('utf-8', {fatal: true}).decode(raw); }
            catch { name = new TextDecoder('shift-jis').decode(raw); }
            if (!name || name === '.' || name === '..' || /[\x00-\x1f/\\:]/.test(name)) throw new Error('the disc contains an unsafe filename.');
            const path = parent.path + name;
            const location = fst.getUint32(i * 12 + 4), amount = fst.getUint32(i * 12 + 8);
            if (type === 1) {
                if (location !== parent.index || amount <= i || amount > parent.end) throw new Error('the disc has an invalid directory.');
                stack.push({index: i, end: amount, path: path + '/'});
            } else {
                if (location > size || amount > size - location) throw new Error('a disc file points outside the image.');
                if (!(pikmin2 ? /^(?:(user|AudioRes|movie|new_screen|System|system|banner|enemy|memoryCard|message|thp)\/|gameConfig\.ini$)/ : /^(dataDir|MovieData)\//).test(path)) continue;
                if (!safePath(path) || paths.has(path)) throw new Error('the disc contains an invalid or duplicate game-data path.');
                if (amount > 256 * 1024 * 1024) throw new Error('a game-data file exceeds the supported size.');
                total += amount;
                if (total > limit) throw new Error('the game data exceeds the supported size.');
                paths.add(path);
                files.push({path, offset: location, size: amount});
            }
        }
        if (!files.some(file => file.path === (pikmin2 ? 'AudioRes/PSound.aaf' : 'dataDir/parms/gamePrms.bin') && file.size > 0))
            throw new Error('the disc is missing pikmin game data.');
        return {gameId, revision, key, description: supported[key], files, bytes: total};
    }

    const api = {scan, safePath, maxBytes, supported};
    (globalThis.Nectar ||= {}).DiscFiles = api;
    if (typeof module !== 'undefined') module.exports = api;
})();
