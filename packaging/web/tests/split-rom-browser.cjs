const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const http = require('node:http');
const {execFileSync} = require('node:child_process');
const puppeteer = require('puppeteer-core');

(async () => {
    const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'nectar-split-rom-'));
    const browser = await puppeteer.launch({
        executablePath: process.env.NECTAR_TEST_CHROME || '/usr/bin/google-chrome',
        headless: true,
        args: ['--no-sandbox', '--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader']
    });
    try {
        for (const threaded of [false, true]) {
            const mode = threaded ? 'threaded' : 'single';
            const destination = path.join(temporary, mode);
            execFileSync('python3', ['packaging/web/package_dist.py', `${process.env.NECTAR_TEST_BUILD_PREFIX || "build-web"}-${mode}/bin`, destination,
                '--rom', path.resolve('build-web-tests/discs/synthetic-zstd.rvz'), '--part-bytes', '512']);
            let corrupt = false, delay = 10, inflight = 0, maximum = 0;
            const requests = [];
            const server = http.createServer((request, response) => {
                if (threaded) {
                    response.setHeader('Cross-Origin-Opener-Policy', 'same-origin');
                    response.setHeader('Cross-Origin-Embedder-Policy', 'require-corp');
                }
                const pathname = new URL(request.url, 'http://localhost').pathname;
                const file = path.resolve(destination, '.' + pathname);
                if (!file.startsWith(destination + path.sep) || !fs.existsSync(file) || !fs.statSync(file).isFile()) {
                    response.writeHead(404); response.end(); return;
                }
                const types = {'.js': 'text/javascript', '.wasm': 'application/wasm', '.html': 'text/html', '.json': 'application/json'};
                response.setHeader('Content-Type', types[path.extname(file)] || 'application/octet-stream');
                if (pathname.includes('/rom/')) {
                    requests.push(pathname); ++inflight; maximum = Math.max(maximum, inflight);
                    response.on('close', () => --inflight);
                    setTimeout(() => {
                        if (response.destroyed) return;
                        const bytes = fs.readFileSync(file);
                        if (corrupt && pathname.endsWith('part-00001.bin')) bytes[0] ^= 255;
                        response.end(bytes);
                    }, delay);
                } else fs.createReadStream(file).on('error', () => response.destroy()).pipe(response);
            });
            await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
            const context = await browser.createBrowserContext();
            try {
                const page = await context.newPage();
                const errors = [];
                page.on('pageerror', error => errors.push(error.message));
                await page.evaluateOnNewDocument(() => {
                    let stored;
                    Object.defineProperty(window, 'Module', {configurable: true, get: () => stored, set: value => {
                        stored = value;
                        if (!value || value.__testHook) return;
                        value.__testHook = true; value.noInitialRun = true;
                        const previous = value.onRuntimeInitialized;
                        value.onRuntimeInitialized = () => {
                            previous?.();
                            window.__files = {
                                game: Array.from(value.FS.readFile('/assets/dataDir/parms/gamePrms.bin')),
                                movie: value.FS.readFile('/assets/MovieData/test.h4m').length
                            };
                        };
                    }});
                });
                await page.goto(`http://127.0.0.1:${server.address().port}/index.html`);
                await page.waitForSelector('#source-packaged', {visible: true});
                assert.equal(await page.$eval('#source-packaged', element => element.textContent), 'play');
                assert.equal(await page.$eval('#source-own', element => element.textContent), 'upload rom');
                assert.equal(await page.evaluate(() => crossOriginIsolated), threaded);
                await page.click('#source-packaged');
                await page.waitForFunction(() => window.__files, {timeout: 60000});
                assert.deepEqual(await page.evaluate(() => window.__files), {game: [1, 2, 3, 4, 5], movie: 8192});
                assert(maximum <= 2, 'more than two rom downloads ran at once');
                assert(requests.length > 1);
                const downloaded = requests.length;
                await page.reload();
                await page.waitForSelector('#source-cached', {visible: true});
                await page.click('#source-cached');
                await page.waitForFunction(() => window.__files, {timeout: 60000});
                assert.equal(requests.length, downloaded, 'saved copy downloaded rom parts again');

                await page.reload();
                await page.waitForSelector('#source-own', {visible: true});
                await (await page.$('#game-source-file')).uploadFile(path.resolve('build-web-tests/discs/synthetic-pal.iso'));
                await page.waitForFunction(() => window.__files, {timeout: 60000});
                assert(await page.evaluate(() => [...document.scripts].some(script => script.src.endsWith('/nectar-pal.js'))));

                corrupt = true;
                await page.reload();
                await page.waitForSelector('#source-packaged', {visible: true});
                await page.click('#source-packaged');
                await page.waitForFunction(() => document.getElementById('source-error').textContent.includes('damaged'));
                const activeKey = () => page.evaluate(async () => {
                    const db = await Nectar.DiscStore.open();
                    try { return (await Nectar.DiscStore.active(db)).key; }
                    finally { db.close(); }
                });
                assert.equal(await activeKey(), 'GPIP01:0');
                corrupt = false; delay = 500;
                await page.click('#source-packaged');
                await page.waitForFunction(() => !document.getElementById('source-cancel').hidden);
                await page.click('#source-cancel');
                await page.waitForFunction(() => document.getElementById('source-error').textContent.includes('canceled'));
                assert.equal(await activeKey(), 'GPIP01:0');
                await page.reload();
                await page.waitForSelector('#source-cached', {visible: true});
                await page.click('#source-cached');
                await page.waitForFunction(() => window.__files, {timeout: 60000});
                assert.deepEqual(errors, []);
                console.log(`${mode}: included parts, bounded downloads, cached reload, upload, corruption and cancellation passed`);
            } finally {
                await context.close();
                await new Promise(resolve => server.close(resolve));
            }
        }
    } finally {
        await browser.close();
        fs.rmSync(temporary, {recursive: true, force: true});
    }
})().catch(error => {console.error(error); process.exitCode = 1;});
