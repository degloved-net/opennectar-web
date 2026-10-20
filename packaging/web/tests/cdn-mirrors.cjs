const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const {webcrypto} = require('node:crypto');
const source = fs.readFileSync('packaging/web/jsdelivr/storage.js', 'utf8');
const root = 'https://cdn.jsdelivr.net/gh/Cirsius/test@1234567890abcdef/cdn';
const bytes = Buffer.from('verified resource');
const sha256 = require('node:crypto').createHash('sha256').update(bytes).digest('hex');
const roots = [root, root.replace('cdn.', 'fastly.'), root.replace('cdn.', 'gcore.')];
function fixture(fetch) {
    const calls = [], cache = new Map();
    const context = {
        Nectar: {CdnConfig: {root, files: {'asset.bin': {bytes: bytes.length, sha256}}},
            TextureLibrary: class {}, SaveTransfer: {}, DiscStore: {}, Discs: {}},
        Blob, AbortController, crypto: webcrypto, clearTimeout,
        setTimeout: (fn, ms) => setTimeout(fn, ms >= 12000 ? 15 : 0),
        fetch: async (url, options) => {calls.push(url); return fetch(url, options, calls.length);}
    };
    vm.runInNewContext(source, context);
    const cdn = context.Nectar.Cdn;
    cdn.storage.get = async (_, key) => cache.get(key);
    cdn.storage.put = async (_, key, value) => cache.set(key, value);
    return {cdn, calls, cache};
}
(async () => {
    const fallback = fixture(async (_, options, n) => {
        if (n === 1) return new Response('', {status: 403});
        if (n === 2) return new Response(Buffer.alloc(bytes.length));
        return new Response(bytes);
    });
    assert.deepEqual(Buffer.from(await (await fallback.cdn.resource('asset.bin')).arrayBuffer()), bytes);
    assert.deepEqual(fallback.calls, roots.map(root => root + '/asset.bin'));
    await fallback.cdn.resource('asset.bin');
    assert.equal(fallback.calls.length, 3);
    fallback.cache.clear();
    await fallback.cdn.resource('asset.bin');
    assert.equal(fallback.calls[3], roots[2] + '/asset.bin');

    const sharded = fixture(async () => new Response(bytes));
    sharded.cdn.config.files['asset.bin'].root = root.replace('@1234567890abcdef', '@anotherbundle');
    await sharded.cdn.resource('asset.bin');
    assert.equal(sharded.calls[0], root.replace('@1234567890abcdef', '@anotherbundle') + '/asset.bin');
    assert(!sharded.calls.some(url => new URL(url).hostname.endsWith('githubusercontent.com')));

    for (const body of [false, true]) {
        const stalled = fixture(async (_, {signal}, n) => {
            if (n > 1) return new Response(bytes);
            if (!body) return new Promise((resolve, reject) => signal.addEventListener('abort', () => reject(signal.reason)));
            return new Response(new ReadableStream({start(controller) {
                controller.enqueue(new Uint8Array([1]));
                signal.addEventListener('abort', () => controller.error(signal.reason));
            }}));
        });
        await stalled.cdn.resource('asset.bin');
        assert.equal(stalled.calls.length, 2);
        assert.equal(stalled.cache.size, 1);
    }

    const oversized = fixture(async (_, options, n) => new Response(n === 1 ? Buffer.alloc(bytes.length + 1) : bytes));
    await oversized.cdn.resource('asset.bin');
    assert.equal(oversized.calls.length, 2);

    const canceled = fixture(async (_, {signal}) => new Promise((resolve, reject) => {
        signal.addEventListener('abort', () => reject(signal.reason));
    }));
    const controller = new AbortController();
    const pending = canceled.cdn.resource('asset.bin', controller.signal);
    await new Promise(resolve => setTimeout(resolve, 1));
    controller.abort();
    await assert.rejects(pending, {name: 'AbortError'});
    assert.equal(canceled.calls.length, 1);
    assert.equal(canceled.cache.size, 0);

    const corrupt = fixture(async () => new Response(Buffer.alloc(bytes.length)));
    await assert.rejects(corrupt.cdn.resource('asset.bin'), /does not match/);
    assert.equal(corrupt.calls.length, 6);
    assert.equal(corrupt.cache.size, 0);
    console.log('cdn mirrors: fallback, verified cache, sticky mirror, header/body stalls, oversized data and cancellation passed');
})().catch(error => {console.error(error); process.exitCode = 1;});
