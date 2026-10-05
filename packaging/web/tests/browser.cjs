const assert=require('node:assert/strict');
const p=require('puppeteer-core');const path=require('path');const fs=require('node:fs');
(async()=>{
 const browser=await p.launch({executablePath:process.env.NECTAR_TEST_CHROME || '/usr/bin/google-chrome',headless:true,args:['--no-sandbox','--use-gl=angle','--use-angle=swiftshader','--enable-unsafe-swiftshader']});
 const downloads=fs.mkdtempSync(path.resolve('build-web-tests/save-export-'));
 try {
  const client=await browser.target().createCDPSession();
  await client.send('Browser.setDownloadBehavior',{behavior:'allow',downloadPath:downloads,eventsEnabled:true});
  const page=await browser.newPage();
  page.on('pageerror',e=>console.error('pageerror',e.message));
  page.on('console',m=>{if(m.type()==='error') console.error('console',m.text())});
  const requests=[];page.on('request',r=>requests.push(r.url()));
  await page.evaluateOnNewDocument(()=>{
   let stored;Object.defineProperty(window,'Module',{configurable:true,get:()=>stored,set:value=>{
    stored=value;if(!value||value.__testHook)return;value.__testHook=true;value.noInitialRun=true;
    const old=value.onRuntimeInitialized;
    value.onRuntimeInitialized=()=>{old?.();window.__files={game:Array.from(value.FS.readFile('/assets/dataDir/parms/gamePrms.bin')),movie:value.FS.readFile('/assets/MovieData/test.h4m').length};};
   }});
  });
  await page.goto(process.argv[2] || 'http://127.0.0.1:8765/nectar.html');
  await page.waitForSelector('#source-own',{visible:true});
  const threaded=process.env.NECTAR_TEST_THREADED==='1';
  assert.deepEqual(await page.evaluate(()=>({isolated:crossOriginIsolated,shared:typeof SharedArrayBuffer})),{isolated:threaded,shared:threaded?'function':'undefined'});
  await (await page.$('#game-source-file')).uploadFile(path.resolve('build-web-tests/discs/synthetic-zstd.rvz'));
  await page.waitForFunction(()=>window.__files,{timeout:60000});
  assert.deepEqual(await page.evaluate(()=>window.__files),{game:[1,2,3,4,5],movie:8192});
  const downloaded=new Promise((resolve,reject)=>{
   const timeout=setTimeout(()=>reject(new Error('save export download timed out')),30000);
   const progress=event=>{
    if(event.state==='inProgress')return;
    clearTimeout(timeout);client.off('Browser.downloadProgress',progress);
    if(event.state==='completed')resolve();else reject(new Error('save export download canceled'));
   };
   client.on('Browser.downloadProgress',progress);
  });
  await page.evaluate(async()=>{
   Module.FS.mkdirTree('/persistent/save/card0');
   Module.FS.writeFile('/persistent/save/card0/test.dat',new Uint8Array([0,255,128,42]));
   Module.FS.writeFile('/persistent/pikmin_settings.conf','synthetic settings');
   const files=new Nectar.GameFiles(Module,document.getElementById('file-picker'),()=>{});
   await files.request(0);
  });
  await downloaded;
  const backup=JSON.parse(fs.readFileSync(path.join(downloads,'nectar-save-backup.json'),'utf8'));
  assert.equal(backup.format,'open-nectar-save');assert.equal(backup.version,1);
  const saved=backup.files.find(file=>file.path==='save/card0/test.dat');
  assert.deepEqual(Buffer.from(saved.data,'base64'),Buffer.from([0,255,128,42]));
  assert.equal(Buffer.from(backup.files.find(file=>file.path==='pikmin_settings.conf').data,'base64').toString(),'synthetic settings');
  assert(!backup.files.some(file=>file.path.startsWith('dataDir/')||file.path.startsWith('Load/')));
  console.log('save export download passed');
  await page.reload();await page.waitForSelector('#source-cached',{visible:true});await page.click('#source-cached');
  await page.waitForFunction(()=>window.__files,{timeout:60000});console.log('cached reload passed');
  await page.reload();await page.waitForSelector('#source-own',{visible:true});
  await (await page.$('#game-source-file')).uploadFile(path.resolve('build-web-tests/discs/synthetic-pal.iso'));
  await page.waitForFunction(()=>window.__files,{timeout:60000});
  assert(requests.some(url=>url.endsWith('/nectar-pal.js')));
  assert.deepEqual(await page.evaluate(()=>window.__files),{game:[1,2,3,4,5],movie:8192});
  await page.reload();await page.waitForSelector('#source-own',{visible:true});
  await (await page.$('#game-source-file')).uploadFile(path.resolve('packaging/web/readme.md'));
  await page.waitForFunction(()=>document.getElementById('source-error').textContent.length>0,{timeout:30000});
  assert.match(await page.$eval('#source-error',el=>el.textContent),/disc format/);
  await page.reload();await page.waitForSelector('#source-cached',{visible:true});await page.click('#source-cached');
  await page.waitForFunction(()=>window.__files,{timeout:60000});console.log('previous import preserved');
  await page.reload();await page.waitForSelector('#source-own',{visible:true});
  await page.evaluate(async()=>{
   const input=document.getElementById('game-source-file');
   const transfer=new DataTransfer();transfer.items.add(new File([new Uint8Array(4*1024*1024)],'cancel.iso'));
   input.files=transfer.files;input.dispatchEvent(new Event('change'));
   document.getElementById('source-cancel').click();
  });
  await page.waitForFunction(()=>document.getElementById('source-error').textContent.includes('canceled'));
  await page.reload();await page.waitForSelector('#source-cached',{visible:true});await page.click('#source-cached');
  await page.waitForFunction(()=>window.__files,{timeout:60000});
  console.log('canceled import preserved previous copy');
 }finally{await browser.close();fs.rmSync(downloads,{recursive:true,force:true});}
})().catch(e=>{console.error(e);process.exitCode=1;});
