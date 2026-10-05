const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const source = fs.readFileSync('packaging/web/loader.js', 'utf8')
    .replace('new BrowserRuntime();', 'globalThis.BrowserRuntime = BrowserRuntime;');
async function startup(build, {isolated = false, sharedMemory = false, packaged = false, key = 'GPIE01:1'} = {}) {
    const requests = [], scripts = [], choices = [];
    const assets = {version:1, bytes:5, discKey:key, outputs:{'nectar.data':'a'.repeat(64)}};
    const context = {
        isSecureContext:true, crossOriginIsolated:isolated, WebAssembly,
        SharedArrayBuffer:sharedMemory ? SharedArrayBuffer : undefined,
        document:{createElement:()=>({getContext:()=>({VERSION:1,MAX_TEXTURE_SIZE:2,
            getParameter:()=>4096,getExtension:()=>null})})},
        Nectar:{SaveTransfer:{read:async()=>null},Discs:{
            choose:async(runtime,bundled)=> {choices.push(bundled);return packaged ? {bundled} : {install:{key}};}, load:async()=>{}}},
        fetch:async url=>{
            requests.push(url);
            if(url==='nectar-build.json') return {ok:true,json:async()=>build};
            return packaged ? {ok:true,json:async()=>assets} : {ok:false,status:404};
        }
    };
    vm.runInNewContext(source,context);
    const runtime=Object.create(context.BrowserRuntime.prototype);
    runtime.display={}; runtime.module={};
    runtime.ui={'source-chooser':{querySelector:()=>({})},'runtime-script':{content:{querySelector:()=>({getAttribute:()=> 'nectar.js'})}}};
    runtime.claimSession=async()=>{}; runtime.loadScript=async name=>scripts.push(name);
    runtime.loadData=async()=>new ArrayBuffer(5);
    runtime.fail=message=>{runtime.error=message;};
    await runtime.start();
    return {requests,scripts,choices,error:runtime.error,module:runtime.module};
}
(async()=>{
    const runtimes={'GPIE01:1':'nectar.js','GPIP01:0':'nectar-pal.js'};
    const single=await startup({threaded:false,runtimes});
    assert.equal(single.error,undefined);
    assert.deepEqual(single.scripts,['nectar.js']);
    assert.equal(typeof single.module.nectarLoadOwnGame,'function');
    const rom={name:'pikmin.rvz',bytes:1,parts:[]};
    const split=await startup({threaded:false,runtimes,rom});
    assert.equal(split.choices.length,1);
    assert.equal(split.choices[0].rom,rom);
    assert.deepEqual(split.scripts,['nectar.js']);
    assert.deepEqual((await startup({threaded:false,runtimes},{key:'GPIP01:0'})).scripts,['nectar-pal.js']);
    const bundle=await startup({threaded:false,runtimes},{packaged:true,key:'GPIP01:0'});
    assert.deepEqual(bundle.scripts,['nectar-assets.js','nectar-pal.js']);
    assert.equal(bundle.module.getPreloadedPackage('nectar.data',5).byteLength,5);
    assert.match((await startup({threaded:true})).error,/cross-origin isolation is required/);
    assert.equal((await startup({threaded:true},{isolated:true,sharedMemory:true})).error,undefined);
    assert.match((await startup({version:3})).error,/cross-origin isolation is required/);
    assert.match((await startup({threaded:'false'})).error,/threading mode is invalid/);
    assert.match((await startup({threaded:false,runtimes:{'GPIE01:1':'other.js'}})).error,/engine list is invalid/);
    const p2runtimes={'GPVE01:0':'nectar.js','GPVP01:0':'nectar-pal.js'};
    const p2=await startup({game:'pikmin2',threaded:false,runtimes:p2runtimes},{key:'GPVE01:0'});
    assert.equal(p2.error,undefined);assert.deepEqual(p2.scripts,['nectar.js']);
    assert.equal(p2.module.preRun.length,1);
    const opened=[];const idb={getDB:(name,callback)=>{opened.push(name);callback();}};
    p2.module.FS={filesystems:{IDBFS:idb}};p2.module.preRun[0]();idb.getDB('/persistent',()=>{});
    assert.deepEqual(opened,['nectar-pikmin2:/persistent']);
    assert.deepEqual((await startup({game:'pikmin2',threaded:false,runtimes:p2runtimes},{key:'GPVP01:0'})).scripts,['nectar-pal.js']);
    console.log('web loader source, mode, region, and save isolation tests passed');
})().catch(error=>{console.error(error);process.exitCode=1;});
