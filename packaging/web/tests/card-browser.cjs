const assert = require('node:assert/strict');
const fs = require('node:fs');
const http = require('node:http');
const path = require('node:path');
const puppeteer = require('puppeteer-core');

const output = path.resolve(process.argv[2] || 'build-web-tests');
const server = http.createServer((request, response) => {
    const pathname = new URL(request.url, 'http://localhost').pathname;
    if (pathname === '/') {
        response.setHeader('Content-Type', 'text/html');
        response.end(`<script>
var Module = {
    noInitialRun: location.search === '?restore',
    nectarStorage: state => { window.storageState = state; },
    onRuntimeInitialized: () => { window.cardReady = true; },
    print: text => {
        console.log(text);
        if (text === 'single-threaded card save tests passed') window.cardPassed = true;
    }
};
</script><script src="/card-browser.js"></script>`);
        return;
    }
    if (pathname !== '/card-browser.js' && pathname !== '/card-browser.wasm') {
        response.writeHead(404).end();
        return;
    }
    response.setHeader('Content-Type', pathname.endsWith('.wasm') ? 'application/wasm' : 'text/javascript');
    fs.createReadStream(path.join(output, pathname.slice(1))).pipe(response);
});

(async () => {
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    let browser;
    try {
        browser = await puppeteer.launch({
            executablePath: process.env.NECTAR_TEST_CHROME || '/usr/bin/google-chrome',
            headless: true, args: ['--no-sandbox']
        });
        const page = await browser.newPage();
        page.on('pageerror', error => console.error(error.message));
        const url = `http://127.0.0.1:${server.address().port}/`;
        await page.goto(url);
        await page.waitForFunction(() => window.cardPassed, {timeout: 20000});
        assert.deepEqual(await page.evaluate(() => ({
            isolated: crossOriginIsolated, shared: typeof SharedArrayBuffer, storage: window.storageState
        })), {isolated: false, shared: 'undefined', storage: 'ready'});
        await page.evaluate(() => Module.nectarSyncSaves());
        await page.goto(url + '?restore');
        await page.waitForFunction(() => window.cardReady, {timeout: 20000});
        assert.equal(await page.evaluate(() => window.storageState), 'ready');
        const saved = await page.evaluate(() => Array.from(Module.FS.readFile('/persistent/save/card0/synthetic-save')));
        assert.equal(saved.length, 0x26000);
        assert.deepEqual(saved.slice(0x6000, 0xe000), Array.from({length: 0x8000}, (_, i) => (i + 3) % 251));
        assert.deepEqual(saved.slice(0x2000, 0x4000), Array.from({length: 0x2000}, (_, i) => (i * 3) % 239));
        console.log('browser card save and reload tests passed');
    } finally {
        if (browser) await browser.close();
        await new Promise(resolve => server.close(resolve));
    }
})().catch(error => {console.error(error); process.exitCode = 1;});
