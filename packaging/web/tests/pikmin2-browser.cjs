const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const http = require('node:http');
const {execFileSync} = require('node:child_process');
const puppeteer = require('puppeteer-core');
(async () => {
    const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'nectar-pikmin2-browser-'));
    const browser = await puppeteer.launch({executablePath:process.env.NECTAR_TEST_CHROME || '/usr/bin/google-chrome',
        headless:true,args:['--no-sandbox','--use-gl=angle','--use-angle=swiftshader','--enable-unsafe-swiftshader']});
    try {
        for (const threaded of [false,true]) {
            const mode = threaded ? 'threaded' : 'single';
            const destination = path.join(temporary,mode);
            execFileSync('python3',['packaging/web/package_dist.py',`build-web-pikmin2-${mode}/bin`,destination]);
            execFileSync('python3',['packaging/web/package_dist.py',(process.env.NECTAR_TEST_SINGLE_BUILD || 'build-web-single/bin'),path.join(destination,'pikmin1')]);
            const server = http.createServer((request,response) => {
                if(threaded) {response.setHeader('Cross-Origin-Opener-Policy','same-origin');response.setHeader('Cross-Origin-Embedder-Policy','require-corp');}
                const pathname = new URL(request.url,'http://localhost').pathname;
                const file = path.resolve(destination,'.'+pathname);
                if(!file.startsWith(destination+path.sep)||!fs.existsSync(file)||!fs.statSync(file).isFile()) {response.writeHead(404).end();return;}
                response.setHeader('Content-Type',({'.js':'text/javascript','.wasm':'application/wasm','.html':'text/html','.json':'application/json'})[path.extname(file)]||'application/octet-stream');
                fs.createReadStream(file).on('error',()=>response.destroy()).pipe(response);
            });
            await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
            const context = await browser.createBrowserContext();
            try {
                const page = await context.newPage();const errors=[];
                page.on('pageerror',e=>errors.push(e.message));
                await page.evaluateOnNewDocument(()=>{
                    let stored;Object.defineProperty(window,'Module',{configurable:true,get:()=>stored,set:value=>{
                        stored=value;value.noInitialRun=true;
                        const old=value.onRuntimeInitialized;value.onRuntimeInitialized=()=>{old?.();window.testReady=true;};
                    }});
                });
                const url=`http://127.0.0.1:${server.address().port}`;
                await page.goto(url+'/index.html');await page.waitForSelector('#source-own',{visible:true});
                assert.match(await page.$eval('.source-formats',el=>el.textContent),/gamecube.*pikmin 2.*usa or europe/);
                assert.equal(await page.evaluate(()=>crossOriginIsolated),threaded);
                await (await page.$('#game-source-file')).uploadFile(path.resolve('build-web-tests/discs/synthetic-pikmin2.iso'));
                await page.waitForFunction(()=>window.testReady,{timeout:90000});
                assert.deepEqual(await page.evaluate(()=>Array.from(Module.FS.readFile('/assets/AudioRes/PSound.aaf'))),[1,2,3,4,5]);
                await page.evaluate(async()=>{
                    Module.FS.mkdirTree('/persistent/save/card0');Module.FS.writeFile('/persistent/save/card0/p2.dat',new Uint8Array([2,4,6]));await Module.nectarSyncSaves();
                    const p2=Nectar.SaveArchive.create(await Module.nectarExportSaves());
                    if(p2.game!=='pikmin2')throw Error('save game missing');
                    const p1={...p2};delete p1.game;
                    try {Nectar.SaveArchive.decode(JSON.stringify(p1));throw Error('wrong game accepted');}catch(error){if(!error.message.includes('other pikmin game'))throw error;}
                });
                await page.goto(url+'/pikmin1/index.html');await page.waitForSelector('#source-own',{visible:true});
                assert.equal(await page.$eval('#source-cached',el=>el.hidden),true);
                await (await page.$('#game-source-file')).uploadFile(path.resolve('build-web-tests/discs/synthetic.iso'));
                await page.waitForFunction(()=>window.testReady,{timeout:90000});
                assert.equal(await page.evaluate(()=>Module.FS.analyzePath('/persistent/save/card0/p2.dat').exists),false);
                await page.evaluate(async()=>{Module.FS.mkdirTree('/persistent/save/card0');Module.FS.writeFile('/persistent/save/card0/p1.dat',new Uint8Array([1,3,5]));await Module.nectarSyncSaves();});
                await page.goto(url+'/index.html');await page.waitForSelector('#source-cached',{visible:true});await page.click('#source-cached');
                await page.waitForFunction(()=>window.testReady,{timeout:90000});
                assert.deepEqual(await page.evaluate(()=>Array.from(Module.FS.readFile('/persistent/save/card0/p2.dat'))),[2,4,6]);
                assert.equal(await page.evaluate(()=>Module.FS.analyzePath('/persistent/save/card0/p1.dat').exists),false);
                const databases=await page.evaluate(async()=>(await indexedDB.databases()).map(db=>db.name));
                assert(databases.includes('/persistent'));assert(databases.includes('nectar-pikmin2:/persistent'));
                assert.deepEqual(errors,[]);
                console.log(mode+': pikmin 2 engine, disc import, cached reload, save persistence and game isolation passed');
            } finally {await context.close();await new Promise(resolve=>server.close(resolve));}
        }
    } finally {await browser.close();fs.rmSync(temporary,{recursive:true,force:true});}
})().catch(error=>{console.error(error);process.exitCode=1;});
