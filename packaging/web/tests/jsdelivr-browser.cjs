const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const http = require('node:http');
const zlib = require('node:zlib');
const {execFileSync} = require('node:child_process');
const puppeteer = require('puppeteer-core');
const base = 'https://cdn.jsdelivr.net/gh/test/fixture@1234567890abcdef1234567890abcdef12345678/cdn';
const temporary = fs.mkdtempSync(path.resolve('build-web-tests/jsdelivr-browser-'));
const pikmin2 = process.env.NECTAR_TEST_GAME === 'pikmin2';
const romFixture = pikmin2 ? 'build-web-tests/discs/synthetic-pikmin2-zstd.rvz' : 'build-web-tests/discs/synthetic-zstd.rvz';
const palFixture = pikmin2 ? 'build-web-tests/discs/synthetic-pikmin2.iso' : 'build-web-tests/discs/synthetic-pal.iso';
const gameFile = pikmin2 ? '/assets/AudioRes/PSound.aaf' : '/assets/dataDir/parms/gamePrms.bin';
const movieFile = pikmin2 ? '/assets/movie/test.thp' : '/assets/MovieData/test.h4m';
const output = path.join(temporary,'dist');
let payload = path.join(output,'cdn');
let corrupt = false, paused = false;
const counts = new Map();
const server = http.createServer((req,res) => {
    const name = new URL(req.url,'http://localhost').pathname.slice(1);
    counts.set(name,(counts.get(name)||0)+1);
    res.setHeader('Access-Control-Allow-Origin','*');
    const file = path.join(payload,name);
    if (!fs.existsSync(file)) {res.writeHead(404).end();return;}
    if (corrupt && name.startsWith('rom/')) {res.end('corrupt');return;}
    const send = () => {
        if (res.destroyed) return;
        res.setHeader('Content-Type',name.endsWith('.wasm')?'application/wasm':'application/octet-stream');
        const stream=fs.createReadStream(file);stream.on('error',()=>res.destroy());stream.pipe(res);
    };
    if (paused && name.startsWith('rom/')) setTimeout(send,200); else send();
});
(async () => {
    let browser;
    try {
        execFileSync('python3',['packaging/web/package_jsdelivr.py',(process.env.NECTAR_TEST_SINGLE_BUILD || 'build-web-single/bin'),output,'--cdn-root',base,
            '--rom',romFixture,'--part-bytes','500','--replace']);
        await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
        const html = path.join(output,'index.html');
        fs.writeFileSync(html,fs.readFileSync(html,'utf8').replaceAll(base,`http://127.0.0.1:${server.address().port}`));
        browser = await puppeteer.launch({executablePath:process.env.NECTAR_TEST_CHROME || '/usr/bin/google-chrome',headless:true,
            args:['--no-sandbox','--use-gl=angle','--use-angle=swiftshader','--enable-unsafe-swiftshader']});
        const page = await browser.newPage();
        page.on('pageerror',error=>console.error('pageerror',error.message));
        await page.evaluateOnNewDocument(() => {
            let stored;
            Object.defineProperty(window,'Module',{configurable:true,get:()=>stored,set:value=>{
                stored=value;value.noInitialRun=true;
                const old=value.onRuntimeInitialized;
                value.onRuntimeInitialized=()=>{old?.();window.testReady=true;};
            }});
        });
        await page.goto('file://' + html);
        await page.waitForSelector('#source-packaged',{visible:true});
        assert.deepEqual(await page.evaluate(()=>({protocol:location.protocol,shared:typeof SharedArrayBuffer,isolated:crossOriginIsolated})),
            {protocol:'file:',shared:'undefined',isolated:false});
        await page.click('#source-packaged');
        await page.waitForFunction(()=>window.testReady || document.getElementById('source-error').textContent || !document.getElementById('report').hidden,{timeout:30000});
        assert(await page.evaluate(()=>window.testReady),await page.$eval('#status',node=>node.textContent)+' '+await page.$eval('#source-error',node=>node.textContent));
        assert.deepEqual(await page.evaluate(file=>Array.from(Module.FS.readFile(file)),gameFile),[1,2,3,4,5]);
        assert.equal(await page.evaluate(file=>Module.FS.readFile(file).length,movieFile),8192);
        assert.equal(await page.evaluate(()=>Module.nectarSnapshot().textures.cacheBudget),32*1024*1024);
        assert.equal(await page.evaluate(()=>Module.nectarTextureLibrary.constructor.name),'IndexedTextureLibrary');
        await page.evaluate(async()=>{
            Module.FS.mkdirTree('/persistent/save/card0');
            Module.FS.writeFile('/persistent/save/card0/test.dat',new Uint8Array([0,255,128,42]));
            await Module.nectarSyncSaves();
        });
        console.log('local index.html, chunked rvz import, and engine startup passed');
        const before = [...counts];
        await page.reload(); await page.waitForSelector('#source-cached',{visible:true}); await page.click('#source-cached');
        await page.waitForFunction(()=>window.testReady,{timeout:30000});
        assert.deepEqual(await page.evaluate(()=>Array.from(Module.FS.readFile('/persistent/save/card0/test.dat'))),[0,255,128,42]);
        assert.deepEqual([...counts],before);
        assert.equal(await page.evaluate(async()=>{
            const backup=Nectar.SaveArchive.create(await Module.nectarExportSaves());
            return backup.files.find(file=>file.path==='save/card0/test.dat').data;
        }),'AP+AKg==');
        console.log('indexeddb save reload, export, and cached runtime without downloads passed');
        await page.reload(); await page.waitForSelector('#source-own',{visible:true});
        await (await page.$('#game-source-file')).uploadFile(path.resolve(palFixture));
        await page.waitForFunction(()=>window.testReady,{timeout:30000});
        assert(counts.has('nectar-pal.js')); assert(counts.has('nectar-pal.wasm'));
        console.log('upload rom and european runtime passed');
        await page.reload(); await page.waitForSelector('#source-packaged',{visible:true});
        await page.evaluate(()=>Nectar.Cdn.storage.clean('resources',key=>!Nectar.Cdn.config.rom.parts.some(name=>Nectar.Cdn.config.files[name].sha256===key)));
        corrupt = true;
        await page.click('#source-packaged');
        await page.waitForFunction(()=>document.getElementById('source-error').textContent.includes('does not match'),{timeout:30000});
        corrupt = false;
        await page.reload(); await page.waitForSelector('#source-cached',{visible:true}); await page.click('#source-cached');
        await page.waitForFunction(()=>window.testReady,{timeout:30000});
        assert.deepEqual(await page.evaluate(()=>Array.from(Module.FS.readFile('/persistent/save/card0/test.dat'))),[0,255,128,42]);
        console.log('corrupt CDN part rejected and previous installation preserved');
        await page.reload(); await page.waitForSelector('#source-packaged',{visible:true});
        paused = true; await page.click('#source-packaged'); await page.click('#source-cancel');
        await page.waitForFunction(()=>document.getElementById('source-error').textContent.includes('canceled'));
        paused = false;
        await page.reload(); await page.waitForSelector('#source-cached',{visible:true}); await page.click('#source-cached');
        await page.waitForFunction(()=>window.testReady,{timeout:30000});
        console.log('canceled CDN download preserved previous installation');

        const build = path.join(temporary,'texture-build'); fs.mkdirSync(build);
        for (const name of ['nectar-build.json','nectar-disc.js','nectar_disc.wasm','nectar-disc-licenses.txt','nectar-icon.png'])
            fs.copyFileSync(path.join((process.env.NECTAR_TEST_SINGLE_BUILD || 'build-web-single/bin'),name),path.join(build,name));
        const info=JSON.parse(fs.readFileSync(path.join(build,'nectar-build.json')));
        if(pikmin2)info.discKey='GPVP01:0';
        info.runtimes={[info.discKey]:'nectar.js'};fs.writeFileSync(path.join(build,'nectar-build.json'),JSON.stringify(info));
        fs.copyFileSync('build-web-tests/textures.js',path.join(build,'nectar.js'));
        fs.copyFileSync('build-web-tests/textures.wasm',path.join(build,'nectar.wasm'));
        const textureOutput=path.join(temporary,'textures');
        execFileSync('python3',['packaging/web/package_jsdelivr.py',build,textureOutput,'--cdn-root',base,
            '--rom',romFixture,'--part-bytes','500']);
        payload=path.join(textureOutput,'cdn');
        const textureHtml=path.join(textureOutput,'index.html');
        fs.writeFileSync(textureHtml,fs.readFileSync(textureHtml,'utf8').replaceAll(base,`http://127.0.0.1:${server.address().port}`));
        await page.goto('file://'+textureHtml);await page.waitForSelector('#source-packaged',{visible:true});await page.click('#source-packaged');
        await page.waitForFunction(()=>window.testReady,{timeout:30000});
        const table=Uint32Array.from({length:256},(_,n)=>{for(let bit=0;bit<8;++bit)n=n&1?0xedb88320^(n>>>1):n>>>1;return n>>>0;});
        const crc=bytes=>{let n=0xffffffff;for(const byte of bytes)n=table[(n^byte)&255]^(n>>>8);return(n^0xffffffff)>>>0;};
        const png=size=>{
            const chunk=(name,data)=>{const bytes=Buffer.alloc(data.length+12);bytes.writeUInt32BE(data.length);bytes.write(name,4);data.copy(bytes,8);bytes.writeUInt32BE(crc(bytes.subarray(4,-4)),bytes.length-4);return bytes;};
            const header=Buffer.alloc(13);header.writeUInt32BE(size);header.writeUInt32BE(size,4);header[8]=8;header[9]=6;
            const pixels=Buffer.alloc((size*4+1)*size);for(let y=0;y<size;++y)for(let x=0;x<size;++x)Buffer.from([10,20,30,255]).copy(pixels,y*(size*4+1)+1+x*4);
            return Buffer.concat([Buffer.from([137,80,78,71,13,10,26,10]),chunk('IHDR',header),chunk('IDAT',zlib.deflateSync(pixels)),chunk('IEND',Buffer.alloc(0))]);
        };
        const zip = entries => {
            const local=[],central=[];let offset=0;
            for(const [name,data] of entries){const text=Buffer.from(name),head=Buffer.alloc(30),record=Buffer.alloc(46);
                head.writeUInt32LE(0x04034b50);head.writeUInt32LE(crc(data),14);head.writeUInt32LE(data.length,18);head.writeUInt32LE(data.length,22);head.writeUInt16LE(text.length,26);
                record.writeUInt32LE(0x02014b50);record.writeUInt32LE(crc(data),16);record.writeUInt32LE(data.length,20);record.writeUInt32LE(data.length,24);record.writeUInt16LE(text.length,28);record.writeUInt32LE(offset,42);
                local.push(head,text,data);central.push(record,text);offset+=head.length+text.length+data.length;}
            const directory=Buffer.concat(central),end=Buffer.alloc(22);end.writeUInt32LE(0x06054b50);end.writeUInt16LE(entries.length,8);end.writeUInt16LE(entries.length,10);end.writeUInt32LE(directory.length,12);end.writeUInt32LE(offset,16);
            return Buffer.concat([...local,directory,end]);
        };
        const padded=Buffer.alloc(2*1024*1024+png(2).length);png(2).copy(padded);
        const archive=zip([['tex1_colour.png',padded],['tex1_colour_mip1.png',png(1)]]);
        const pack=await page.evaluate(bytes=>Module.nectarTextureLibrary.installZip(new File([new Uint8Array(bytes)],'test.zip')),Array.from(archive));
        assert.equal(pack.entries.find(entry=>entry.name==='tex1_colour.png').chunks,3);
        assert.equal(await page.evaluate(()=>Module.nectarTextureSnapshot().cachedBytes),0);
        assert.equal(await page.evaluate(id=>Module.FS.readFile('/texture-packs/'+id+'/tex1_colour.png').length,pack.id),0);
        await page.evaluate(id=>Module.nectarTextureLibrary.select(id),pack.id);
        assert.equal(await page.evaluate(()=>Module.ccall('testRequestTexture','number',['string'],['tex1_colour'])),0);
        await page.waitForFunction(()=>Module._testReadyCount()>0);
        assert.equal(await page.evaluate(()=>Module._testProcessTexture()),2);
        assert.equal(await page.evaluate(()=>Module._testFirstPixel()>>>0),0xff1e140a);
        await page.evaluate(()=>{Nectar.TextureLibrary.cacheBytes=1;Module.nectarTextureLibrary.activate('');});
        await page.evaluate(id=>Module.nectarTextureLibrary.select(id),pack.id);
        await page.evaluate(()=>Module.ccall('testRequestTexture','number',['string'],['tex1_colour']));
        await page.waitForFunction(()=>Module._testReadyCount()>0);
        assert.equal(await page.evaluate(()=>Module._testProcessTexture()),2);
        assert.equal(await page.evaluate(()=>Module.nectarTextureSnapshot().cachedBytes),0);
        const broken=zip([['tex1_broken.png',png(1)]]);broken[30+'tex1_broken.png'.length+15]^=1;
        const error=await page.evaluate(async bytes=>{try{await Module.nectarTextureLibrary.installZip(new File([new Uint8Array(bytes)],'broken.zip'));return'';}catch(error){return error.message;}},Array.from(broken));
        assert.match(error,/damaged texture/);
        const orphaned=await page.evaluate(async()=>{
            const directories=new Set(Module.nectarTextureLibrary.list().map(pack=>pack.directory));
            const keys=await Nectar.Cdn.storage.access('pack-files','readonly',store=>store.getAllKeys());
            return keys.some(key=>!directories.has(key[0]));
        });assert.equal(orphaned,false);
        console.log('indexeddb pack streaming, native PNG/mip uploads, bounded cache, and rollback passed');
        await page.reload();await page.waitForSelector('#source-cached',{visible:true});await page.click('#source-cached');
        await page.waitForFunction(()=>window.testReady,{timeout:30000});
        assert.equal(await page.evaluate(()=>Module.nectarTextureSnapshot().cachedBytes),0);
        await page.evaluate(id=>Module.nectarTextureLibrary.select(id),pack.id);
        await page.evaluate(()=>Module.ccall('testRequestTexture','number',['string'],['tex1_colour']));
        await page.waitForFunction(()=>Module._testReadyCount()>0);
        assert.equal(await page.evaluate(()=>Module._testProcessTexture()),2);
        await page.evaluate(()=>Nectar.SaveTransfer.write(JSON.stringify(Nectar.SaveArchive.create([{path:'save/card0/test.dat',bytes:new Uint8Array([7,8,9])}]))));
        await page.reload();await page.waitForSelector('#source-cached',{visible:true});await page.click('#source-cached');
        await page.waitForFunction(()=>window.testReady,{timeout:30000});
        assert.deepEqual(await page.evaluate(()=>Array.from(Module.FS.readFile('/persistent/save/card0/test.dat'))),[7,8,9]);
        console.log('pack reload and staged save restoration passed');
    } finally {
        if (browser) await browser.close();
        if (server.listening) await new Promise(resolve=>server.close(resolve));
        fs.rmSync(temporary,{recursive:true,force:true});
    }
})().catch(error=>{console.error(error);process.exitCode=1;});
