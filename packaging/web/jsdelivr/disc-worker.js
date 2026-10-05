let sequence = 0;
const writes = new Map();
function store(operation, args) {
    const request = ++sequence;
    return new Promise((resolve, reject) => {
        writes.set(request, {resolve, reject});
        postMessage({type: 'store', request, operation, args});
    });
}
self.onmessage = async ({data}) => {
    if (data.request) {
        const pending = writes.get(data.request);
        writes.delete(data.request);
        if (data.error) pending.reject(new Error(data.error)); else pending.resolve();
        return;
    }
    const {file, id, supported, wasm} = data;
    let decoder;
    try {
        decoder = await NectarDisc({instantiateWasm: (imports, receive) => {
            WebAssembly.instantiate(wasm, imports).then(result => receive(result.instance, result.module))
                .catch(error => postMessage({type: 'error', text: String(error.message || error)}));
            return {};
        }});
        decoder.FS.mkdir('/disc');
        decoder.FS.mount(decoder.FS.filesystems.WORKERFS, {blobs: [{name: 'image', data: file}]}, '/disc');
        if (!decoder.ccall('nectar_disc_open', 'number', ['string'], ['/disc/image']))
            throw new Error(decoder.ccall('nectar_disc_error', 'string', [], []));
        const read = (offset, size) => {
            if (!decoder._nectar_disc_read(offset, size))
                throw new Error(decoder.ccall('nectar_disc_error', 'string', [], []));
            const pointer = decoder._nectar_disc_buffer();
            return decoder.HEAPU8.slice(pointer, pointer + size);
        };
        const disc = Nectar.DiscFiles.scan(read, decoder._nectar_disc_size());
        if (!Object.hasOwn(supported, disc.key)) throw new Error('this launcher does not include the engine for ' + disc.description);
        const install = {...disc, id, name: file.name, state: 'staging'};
        await store('install', {install});
        let completed = 0;
        for (const entry of disc.files) {
            for (let offset = 0, part = 0; offset < entry.size; offset += 1024 * 1024, ++part) {
                const bytes = read(entry.offset + offset, Math.min(1024 * 1024, entry.size - offset));
                await store('file', {id, path: entry.path, part, blob: new Blob([bytes])});
            }
            completed += entry.size;
            postMessage({type: 'progress', done: completed, total: disc.bytes, text: 'importing ' + entry.path});
        }
        postMessage({type: 'ready', install});
    } catch (error) {
        postMessage({type: 'error', text: String(error.message || error)});
    } finally { decoder?._nectar_disc_close(); }
};
