importScripts('nectar-disc.js', 'nectar-disc-files.js', 'nectar-disc-store.js');

self.onmessage = async ({data: {file, id, supported, game}}) => {
    Nectar.game = game;
    let decoder, database;
    try {
        postMessage({type: 'progress', text: 'opening your game copy…'});
        decoder = await NectarDisc({locateFile: name => new URL(name, self.location.href).href});
        decoder.FS.mkdir('/disc');
        decoder.FS.mount(decoder.FS.filesystems.WORKERFS, {blobs: [{name: 'image', data: file}]}, '/disc');
        if (!decoder.ccall('nectar_disc_open', 'number', ['string'], ['/disc/image'])) throw new Error(decoder.ccall('nectar_disc_error', 'string', [], []));
        const read = (offset, size) => {
            if (!decoder._nectar_disc_read(offset, size)) throw new Error(decoder.ccall('nectar_disc_error', 'string', [], []));
            const pointer = decoder._nectar_disc_buffer();
            return decoder.HEAPU8.slice(pointer, pointer + size);
        };
        const disc = Nectar.DiscFiles.scan(read, decoder._nectar_disc_size());
        if (!Object.hasOwn(supported, disc.key)) throw new Error('this web build does not include the engine for ' + disc.description + '.');
        database = await Nectar.DiscStore.open();
        const install = {...disc, id, name: file.name, state: 'staging'};
        await Nectar.DiscStore.write(database, 'installs', id, install);
        let completed = 0;
        for (const entry of disc.files) {
            const chunks = [];
            for (let offset = 0; offset < entry.size; offset += 1024 * 1024) {
                chunks.push(read(entry.offset + offset, Math.min(1024 * 1024, entry.size - offset)));
            }
            const blob = new Blob(chunks);
            await Nectar.DiscStore.write(database, 'files', [id, entry.path], blob);
            completed += entry.size;
            postMessage({type: 'progress', done: completed, total: disc.bytes, text: 'importing ' + entry.path});
        }
        postMessage({type: 'ready', install});
    } catch (error) {
        if (database) await Nectar.DiscStore.remove(database, id).catch(() => {});
        postMessage({type: 'error', text: error.name === 'QuotaExceededError'
            ? 'the browser ran out of space for game data. free some storage and try again.' : String(error.message || error)});
    } finally {
        decoder?._nectar_disc_close();
        database?.close();
    }
};
