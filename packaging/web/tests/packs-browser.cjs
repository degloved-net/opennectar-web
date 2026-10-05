const assert = require('node:assert/strict');
const fs = require('node:fs');
const http = require('node:http');
const path = require('node:path');
const zlib = require('node:zlib');
const puppeteer = require('puppeteer-core');
const output = path.resolve(process.argv[2] || 'build-web-tests');
const table = Uint32Array.from({length: 256}, (_, n) => {
    for (let bit = 0; bit < 8; ++bit) n = n & 1 ? 0xedb88320 ^ (n >>> 1) : n >>> 1;
    return n >>> 0;
});
function crc(bytes) { let value = 0xffffffff; for (const byte of bytes) value = table[(value ^ byte) & 255] ^ (value >>> 8); return (value ^ 0xffffffff) >>> 0; }
function png(width, height) {
    const chunk = (name, data) => {
        const result = Buffer.alloc(data.length + 12); result.writeUInt32BE(data.length);
        result.write(name, 4); data.copy(result, 8); result.writeUInt32BE(crc(result.subarray(4, -4)), result.length - 4); return result;
    };
    const header = Buffer.alloc(13); header.writeUInt32BE(width); header.writeUInt32BE(height, 4); header[8] = 8; header[9] = 6;
    const pixel = Buffer.from([10,20,30,255]);
    const pixels = Buffer.alloc((width * 4 + 1) * height);
    for (let y = 0; y < height; ++y) for (let x = 0; x < width; ++x) pixel.copy(pixels, y * (width * 4 + 1) + 1 + x * 4);
    return Buffer.concat([Buffer.from([137,80,78,71,13,10,26,10]), chunk('IHDR', header), chunk('IDAT', zlib.deflateSync(pixels)), chunk('IEND', Buffer.alloc(0))]);
}
function dds() {
    const header = Buffer.alloc(148); header.write('DDS '); header.writeUInt32LE(124, 4);
    header.writeUInt32LE(4, 12); header.writeUInt32LE(4, 16); header.writeUInt32LE(2, 28);
    header.writeUInt32LE(32, 76); header.write('DX10', 84); header.writeUInt32LE(28, 128);
    return Buffer.concat([header, Buffer.from(Array.from({length: 20}, () => [10,20,30,255]).flat())]);
}
function zip(files, deflate = true) {
    const local = [], central = []; let offset = 0;
    for (const [name, bytes] of files) {
        const text = Buffer.from(name); const compressed = deflate ? zlib.deflateRawSync(bytes) : bytes;
        const header = Buffer.alloc(30); header.writeUInt32LE(0x04034b50); header.writeUInt16LE(deflate ? 8 : 0, 8);
        header.writeUInt32LE(crc(bytes), 14); header.writeUInt32LE(compressed.length, 18); header.writeUInt32LE(bytes.length, 22); header.writeUInt16LE(text.length, 26);
        local.push(header, text, compressed);
        const record = Buffer.alloc(46); record.writeUInt32LE(0x02014b50); record.writeUInt16LE(deflate ? 8 : 0, 10);
        record.writeUInt32LE(crc(bytes), 16); record.writeUInt32LE(compressed.length, 20); record.writeUInt32LE(bytes.length, 24); record.writeUInt16LE(text.length, 28); record.writeUInt32LE(offset, 42);
        central.push(record, text); offset += header.length + text.length + compressed.length;
    }
    const directory = Buffer.concat(central); const end = Buffer.alloc(22); end.writeUInt32LE(0x06054b50);
    end.writeUInt16LE(files.length, 8); end.writeUInt16LE(files.length, 10); end.writeUInt32LE(directory.length, 12); end.writeUInt32LE(offset, 16);
    return Buffer.concat([...local, directory, end]);
}
const assets = new Map([
    ['tex1_colour.png', png(4,4)], ['tex1_colour_mip1.png', png(2,2)], ['tex1_colour_mip2.png', png(1,1)],
    ['tex1_dds.dds', dds()], ['tex1_small.png', png(1,1)]
]);
const valid = zip([...assets].map(([name, bytes]) => ['wrapper/GPIE01/' + name, bytes]));
const broken = zip([['tex1_broken.png', png(1,1)]], false); broken[30 + 'tex1_broken.png'.length + 15] ^= 1;
const archives = new Map([['/valid.zip', valid], ['/broken.zip', broken]]);
const server = http.createServer((req, res) => {
    const url = new URL(req.url, 'http://localhost');
    if (archives.has(url.pathname)) {res.end(archives.get(url.pathname)); return;}
    if (url.pathname === '/seed') {res.setHeader('Content-Type','text/html'); res.end('<title>storage fixture</title>'); return;}
    if (url.pathname === '/') {
        res.setHeader('Content-Type','text/html');
        res.end(`<script src="/nectar-packs.js"></script><script>
var Module = {noInitialRun: true, nectarRecord: (level, text) => { (window.records ||= []).push(text); },
    nectarPrepareTextures: fs => { Module.FS = fs; return new Nectar.TextureLibrary(Module).initialize(); },
    onRuntimeInitialized: () => { window.ready = true; }};
</script><script src="/textures.js"></script>`); return;
    }
    const files = {'/nectar-packs.js':'packaging/web/packs.js', '/nectar-pack-worker.js':'packaging/web/pack-worker.js',
        '/textures.js':path.join(output,'textures.js'), '/textures.wasm':path.join(output,'textures.wasm')};
    const file = files[url.pathname]; if (!file) {res.writeHead(404).end(); return;}
    res.setHeader('Content-Type', file.endsWith('.wasm') ? 'application/wasm' : 'text/javascript'); fs.createReadStream(file).pipe(res);
});
(async () => {
    await new Promise(resolve => server.listen(0,'127.0.0.1',resolve));
    let browser;
    try {
        browser = await puppeteer.launch({executablePath:process.env.NECTAR_TEST_CHROME || '/usr/bin/google-chrome',headless:true,args:['--no-sandbox']});
        const page = await browser.newPage(); page.on('pageerror', error => console.error('pageerror', error.message));
        const base = `http://127.0.0.1:${server.address().port}`;
        await page.goto(base + '/seed');
        await page.evaluate(async bytes => {
            const db = await new Promise((resolve,reject) => {
                const item = indexedDB.open('/persistent',21);
                item.onupgradeneeded = () => item.result.createObjectStore('FILE_DATA').createIndex('timestamp','timestamp');
                item.onsuccess = () => resolve(item.result); item.onerror = () => reject(item.error);
            });
            const transaction = db.transaction('FILE_DATA','readwrite'); const store = transaction.objectStore('FILE_DATA');
            const put = (name, contents) => store.put({timestamp:new Date(),mode:contents ? 0o100666 : 0o40777,...(contents ? {contents:new Uint8Array(contents)} : {})},name);
            for (const name of ['/persistent/save','/persistent/save/card0','/persistent/Load','/persistent/Load/Textures','/persistent/Load/Textures/web-legacy','/persistent/Load/Textures/web-invalid']) put(name);
            put('/persistent/save/card0/test.dat',[1,2,3,255]);
            put('/persistent/pikmin_settings.conf',Array.from(new TextEncoder().encode('texturePackEnabled = 1\ntexturePack = web-legacy\n')));
            put('/persistent/Load/Textures/web-legacy/tex1_legacy.png',bytes);
            put('/persistent/Load/Textures/web-invalid/tex1_invalid.png',[1,2,3]);
            await new Promise((resolve,reject) => {transaction.oncomplete=resolve;transaction.onerror=()=>reject(transaction.error);}); db.close();
        }, Array.from(png(1,1)));
        await page.goto(base + '/'); await page.waitForFunction(() => window.ready,{timeout:30000});
        assert.deepEqual(await page.evaluate(() => ({shared:typeof SharedArrayBuffer,isolated:crossOriginIsolated})),{shared:'undefined',isolated:false});
        assert.equal(await page.evaluate(() => Module.FS.readFile('/texture-packs/web-legacy/tex1_legacy.png').length),0);
        assert.deepEqual(await page.evaluate(() => Array.from(Module.FS.readFile('/persistent/save/card0/test.dat'))),[1,2,3,255]);
        assert(await page.evaluate(() => window.records.some(text => text.includes('could not migrate web-invalid'))));
        await page.evaluate(() => Module.nectarSyncSaves());
        const remaining = await page.evaluate(async () => {
            const db = await new Promise(resolve => {const item=indexedDB.open('/persistent');item.onsuccess=()=>resolve(item.result);});
            const keys = await new Promise(resolve => {const item=db.transaction('FILE_DATA').objectStore('FILE_DATA').getAllKeys();item.onsuccess=()=>resolve(item.result);}); db.close(); return keys;
        });
        assert(!remaining.some(key => key.includes('web-legacy'))); assert(remaining.some(key => key.includes('web-invalid')));
        console.log('legacy migration, failure preservation, and save isolation passed');
        await page.evaluate(() => Module.nectarTextureLibrary.select('web-legacy'));
        assert.equal(await page.evaluate(() => Module.ccall('testRequestTexture','number',['string'],['tex1_legacy'])),0);
        await page.waitForFunction(() => Module._testReadyCount() > 0);
        assert.equal(await page.evaluate(() => Module._testProcessTexture()),1);
        assert.equal(await page.evaluate(() => Module._testFirstPixel() >>> 0),0xff1e140a);
        const pack = await page.evaluate(async () => {
            const blob=await (await fetch('/valid.zip')).blob();
            return Module.nectarTextureLibrary.installZip(new File([blob],'synthetic.zip'));
        });
        assert.equal(pack.files,5);
        for (const entry of pack.entries) assert.equal(await page.evaluate(({id,name}) => Module.FS.readFile('/texture-packs/'+id+'/'+name).length,{id:pack.id,name:entry.name}),0);
        await page.evaluate(id => Module.nectarTextureLibrary.select(id),pack.id);
        for (const [name, uploads, bytes] of [['tex1_colour',3,84],['tex1_dds',2,80]]) {
            assert.equal(await page.evaluate(name => Module.ccall('testRequestTexture','number',['string'],[name]),name),0);
            await page.waitForFunction(() => Module._testReadyCount() > 0);
            assert.equal(await page.evaluate(() => Module._testProcessTexture()),uploads);
            assert.equal(await page.evaluate(() => Module._testUploadedBytes()),bytes);
            assert.equal(await page.evaluate(() => Module._testFirstPixel() >>> 0),0xff1e140a);
        }
        console.log('on-demand native png, dds, and mip uploads passed');
        const failure = await page.evaluate(async () => {
            try {await Module.nectarTextureLibrary.installZip(new File([await (await fetch('/broken.zip')).blob()],'broken.zip'));return null;}
            catch(error){return error.message;}
        });
        assert.match(failure,/damaged texture/);
        assert.equal(await page.evaluate(() => Module.nectarTextureLibrary.list().length),2);
        await page.evaluate(() => {Nectar.TextureLibrary.cacheBytes=1;Module.nectarTextureLibrary.activate('');});
        await page.evaluate(id => Module.nectarTextureLibrary.select(id),pack.id);
        await page.evaluate(() => Module.ccall('testRequestTexture','number',['string'],['tex1_colour']));
        await page.waitForFunction(() => Module._testReadyCount() > 0);
        assert.equal(await page.evaluate(() => Module._testProcessTexture()),3);
        assert.equal(await page.evaluate(() => Module.nectarTextureSnapshot().cachedBytes),0);
        assert.equal(await page.evaluate(() => Module.FS.readFile('/texture-packs/'+Module.nectarTextureLibrary.active+'/tex1_colour.png').length),0);
        console.log('failed import rollback and oversized cache release passed');
        await page.evaluate(async () => {
            const library=Module.nectarTextureLibrary;
            const rpc=library.rpc.bind(library);
            library.rpc=(operation,args)=>operation==='read' ? rpc(operation,args).then(result=>new Promise(resolve=>setTimeout(()=>resolve(result),50))) : rpc(operation,args);
            Module.ccall('testRequestTexture','number',['string'],['tex1_colour']);
            await library.select('web-legacy');
            await new Promise(resolve=>setTimeout(resolve,100));
            library.rpc=rpc;
        });
        assert.equal(await page.evaluate(() => Module.nectarTextureSnapshot().active),'web-legacy');
        assert.equal(await page.evaluate(() => Module.nectarTextureSnapshot().cachedBytes),0);
        assert.equal(await page.evaluate(() => Module._testReadyCount()),0);
        await page.evaluate(id=>Module.nectarTextureLibrary.select(id),pack.id);
        await page.evaluate(() => {
            for (const name of ['tex1_colour','tex1_dds','tex1_small']) Module.ccall('testRequestTexture','number',['string'],[name]);
        });
        for (const uploads of [3,2,1]) {
            await page.waitForFunction(()=>Module._testReadyCount()>0);
            await new Promise(resolve=>setTimeout(resolve,20));
            assert.equal(await page.evaluate(()=>Module._testReadyCount()),1);
            assert.equal(await page.evaluate(()=>Module._testProcessTexture()),uploads);
            assert.equal(await page.evaluate(()=>Module.nectarTextureSnapshot().cachedBytes),0);
        }
        console.log('pack switching cancels stale reads and upload backpressure passed');
        if (process.env.NECTAR_TEST_LARGE_PACK) {
            const padded=Buffer.alloc(62*1024*1024); png(1,1).copy(padded);
            archives.set('/large.zip',zip(Array.from({length:9},(_,i)=>['tex1_large'+i+'.png',padded])));
            const large = await page.evaluate(async () => Module.nectarTextureLibrary.installZip(new File([await (await fetch('/large.zip')).blob()],'large.zip')));
            assert(large.bytes > 512*1024*1024);
            assert.equal(await page.evaluate(() => Module.nectarTextureSnapshot().cachedBytes),0);
            for (const entry of large.entries) assert.equal(await page.evaluate(({id,name})=>Module.FS.readFile('/texture-packs/'+id+'/'+name).length,{id:large.id,name:entry.name}),0);
            console.log('558 mib extracted pack streamed to disk without resident file data');
        }
        await page.reload(); await page.waitForFunction(() => window.ready,{timeout:30000});
        assert.equal(await page.evaluate(() => Module.nectarTextureSnapshot().cachedBytes),0);
        assert.deepEqual(await page.evaluate(() => Array.from(Module.FS.readFile('/persistent/save/card0/test.dat'))),[1,2,3,255]);
        await page.evaluate(id => Module.nectarTextureLibrary.select(id),pack.id);
        await page.evaluate(() => Module.ccall('testRequestTexture','number',['string'],['tex1_dds']));
        await page.waitForFunction(() => Module._testReadyCount()>0);
        assert.equal(await page.evaluate(() => Module._testProcessTexture()),2);
        console.log('disk pack reload and saves preserved');
        archives.set('/concurrent.zip', zip(Array.from({length:300},(_,i)=>['tex1_concurrent'+i+'.png',png(1,1)])));
        const concurrent = await page.evaluate(async () => {
            const storage = () => {
                const worker = new Worker('nectar-pack-worker.js');
                let serial = 0, progress;
                const requests = new Map();
                worker.onmessage = ({data}) => {
                    if (data.progress !== undefined) {progress?.(); return;}
                    const item = requests.get(data.id); requests.delete(data.id);
                    if (data.error) item.reject(new Error(data.error)); else item.resolve(data.result);
                };
                return {worker, onProgress: callback => {progress=callback;},
                    rpc: (operation,args={}) => new Promise((resolve,reject) => {
                        const id=++serial; requests.set(id,{resolve,reject});worker.postMessage({id,operation,...args});
                    })};
            };
            const writer=storage(); let reader, restored;
            try {
                await writer.rpc('initialize');
                writer.onProgress(() => {
                    if (reader) return;
                    reader=storage(); restored=reader.rpc('initialize');
                });
                const file=new File([await (await fetch('/concurrent.zip')).blob()],'concurrent.zip');
                const pack=await writer.rpc('install',{file});
                const result=await restored;
                const files=await reader.rpc('read',{pack:pack.id,names:['tex1_concurrent299.png']});
                return {visible:result.packs.some(item=>item.id===pack.id),files:pack.files,bytes:files[0].bytes.byteLength};
            } finally {writer.worker.terminate();reader?.worker.terminate();}
        });
        assert.deepEqual(concurrent,{visible:true,files:300,bytes:png(1,1).length});
        console.log('concurrent import and startup cleanup preserved disk files');
    } finally {
        if (browser) await browser.close(); await new Promise(resolve=>server.close(resolve));
    }
})().catch(error=>{console.error(error);process.exitCode=1;});
