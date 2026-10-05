let sequence = 0;
const writes = new Map();
function store(args) {
    const request = ++sequence;
    return new Promise((resolve, reject) => {
        writes.set(request, {resolve, reject});
        postMessage({type: 'store', request, args});
    });
}
self.onmessage = async ({data}) => {
    if (data.request) {
        const pending = writes.get(data.request);
        writes.delete(data.request);
        if (data.error) pending.reject(new Error(data.error)); else pending.resolve();
        return;
    }
    try {
        const archive = await Nectar.PackArchive.open(data.file);
        const plan = Nectar.TextureLibrary.plan(archive.entries);
        const entries = [];
        for (const {entry, name} of plan.files) {
            const header = new Uint8Array(Math.min(128, entry.size));
            let captured = 0, used = 0, part = 0;
            let buffer = new Uint8Array(1024 * 1024);
            const flush = async () => {
                if (!used) return;
                await store({directory: data.directory, name, part: part++, blob: new Blob([buffer.subarray(0, used)])});
                used = 0;
            };
            await archive.consume(entry, async chunk => {
                const take = Math.min(header.length - captured, chunk.length);
                if (take) {header.set(chunk.subarray(0, take), captured); captured += take;}
                let offset = 0;
                while (offset < chunk.length) {
                    const amount = Math.min(buffer.length - used, chunk.length - offset);
                    buffer.set(chunk.subarray(offset, offset + amount), used);
                    offset += amount; used += amount;
                    if (used === buffer.length) await flush();
                }
            });
            await flush();
            buffer = null;
            const dimensions = Nectar.TextureLibrary.validateTexture(name, header);
            entries.push({name, size: entry.size, chunks: part, ...dimensions});
            postMessage({type: 'progress', done: entries.length, total: plan.files.length});
        }
        postMessage({type: 'ready', entries, bytes: plan.bytes});
    } catch (error) { postMessage({type: 'error', text: String(error.message || error)}); }
};
