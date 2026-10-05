const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const DiscFiles = require('./disc-files.js');

async function prepare(image, decoderDirectory, cacheDirectory) {
    image = path.resolve(image);
    const hash = crypto.createHash('sha256');
    for await (const chunk of fs.createReadStream(image)) hash.update(chunk);
    const fingerprint = hash.digest('hex');
    const root = path.join(path.resolve(cacheDirectory), fingerprint);
    if (fs.existsSync(root)) {
        const manifest = JSON.parse(fs.readFileSync(path.join(root, 'nectar-private-disc.json'), 'utf8'));
        if (manifest.source !== fingerprint || !Array.isArray(manifest.files) || !manifest.files.every(file => DiscFiles.safePath(file.path) && Number.isSafeInteger(file.size) && file.size >= 0 && fs.statSync(path.join(root, file.path)).size === file.size)) {
            throw new Error('the cached extraction is incomplete. remove ' + root + ' and try again.');
        }
        return manifest.gameId?.startsWith('GPV') ? root : path.join(root, 'dataDir');
    }
    fs.mkdirSync(path.dirname(root), {recursive: true});
    const stage = fs.mkdtempSync(path.join(path.dirname(root), '.staging-'));
    let decoder;
    try {
        const factory = require(path.join(path.resolve(decoderDirectory), 'nectar-disc.js'));
        decoder = await factory();
        decoder.FS.mkdir('/disc');
        decoder.FS.mount(decoder.FS.filesystems.NODEFS, {root: path.dirname(image)}, '/disc');
        if (!decoder.ccall('nectar_disc_open', 'number', ['string'], ['/disc/' + path.basename(image)])) {
            throw new Error(decoder.ccall('nectar_disc_error', 'string', [], []));
        }
        const read = (offset, size) => {
            if (!decoder._nectar_disc_read(offset, size)) throw new Error(decoder.ccall('nectar_disc_error', 'string', [], []));
            const pointer = decoder._nectar_disc_buffer();
            return decoder.HEAPU8.subarray(pointer, pointer + size);
        };
        const disc = DiscFiles.scan(read, decoder._nectar_disc_size());
        console.error('extracting your copy of ' + disc.description + '…');
        for (const entry of disc.files) {
            const destination = path.join(stage, entry.path);
            fs.mkdirSync(path.dirname(destination), {recursive: true});
            const fd = fs.openSync(destination, 'wx');
            try {
                for (let offset = 0; offset < entry.size; offset += 1024 * 1024) {
                    const bytes = read(entry.offset + offset, Math.min(1024 * 1024, entry.size - offset));
                    let written = 0;
                    while (written < bytes.length) {
                        const count = fs.writeSync(fd, bytes, written, bytes.length - written);
                        if (!count) throw new Error('could not write the extracted data.');
                        written += count;
                    }
                }
            } finally { fs.closeSync(fd); }
        }
        fs.writeFileSync(path.join(stage, '.pikmin-assets'), `${disc.gameId} revision=${disc.revision}\n`);
        fs.writeFileSync(path.join(stage, 'nectar-private-disc.json'), JSON.stringify({...disc, source: fingerprint}));
        fs.renameSync(stage, root);
        return disc.gameId.startsWith('GPV') ? root : path.join(root, 'dataDir');
    } finally {
        decoder?._nectar_disc_close();
        fs.rmSync(stage, {recursive: true, force: true});
    }
}

if (require.main === module) {
    prepare(...process.argv.slice(2)).then(result => console.log(result))
        .catch(error => { console.error('disc import failed: ' + error.message); process.exitCode = 1; });
}
module.exports = {prepare};
