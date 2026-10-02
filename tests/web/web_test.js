// The browser uploader against the real transfer protocol (usb/usb_cdc.c)
// and file system, through a mocked navigator.serial in headless Chromium.
const { chromium } = require('playwright');
const { spawn } = require('child_process');
const crypto = require('crypto');
// usage: node web_test.js PATH/TO/cdc_pipe  (the device side, on stdin/stdout)
const path = require('path');
const { pathToFileURL } = require('url');
const PAGE = pathToFileURL(path.join(__dirname, '..', '..', 'tools', 'web', 'uploader.html')).href;
let fails = 0;
const check = (c, w) => { console.log(`  ${c ? 'ok  ' : 'FAIL'} ${w}`); if (!c) fails++; };

(async () => {
  const dev = spawn(path.resolve(process.argv[2]));
  let out = Buffer.alloc(0);
  dev.stdout.on('data', d => { out = Buffer.concat([out, d]); });
  const browser = await chromium.launch();
  const page = await browser.newPage({ locale: 'nl-NL' });
  await page.exposeFunction('serialWrite', b64 => { dev.stdin.write(Buffer.from(b64, 'base64')); });
  await page.exposeFunction('serialRead', () => { if (!out.length) return ''; const b = out; out = Buffer.alloc(0); return b.toString('base64'); });
  await page.addInitScript(() => {
    const port = {
      async open(o) { window.__baud = o.baudRate; },
      async close() {},
      readable: new ReadableStream({ async pull(c) {
        for (;;) {
          const b64 = await window.serialRead();
          if (b64) { c.enqueue(Uint8Array.from(atob(b64), x => x.charCodeAt(0))); return; }
          await new Promise(r => setTimeout(r, 5));
        }
      } }),
      writable: new WritableStream({ async write(chunk) {
        let s = ''; for (let i = 0; i < chunk.length; i += 8192) s += String.fromCharCode(...chunk.subarray(i, i + 8192));
        await window.serialWrite(btoa(s));
      } }),
    };
    Object.defineProperty(navigator, 'serial', { configurable: true, value: {
      addEventListener() {}, async requestPort(o) { window.__filters = o.filters; return port; } } });
  });
  page.on('dialog', d => d.accept());
  page.on('pageerror', e => { console.log('  page error:', e.message); fails++; });
  await page.goto(PAGE);

  check(await page.textContent('h1') === 'NumWorks OS bestanden', 'Dutch browser: Dutch page');
  await page.click('#connect');
  await page.waitForSelector('#status.ok');
  const filt = await page.evaluate(() => window.__filters);
  check(filt && filt[0].usbVendorId === 0x1209 && filt[0].usbProductId === 0x0001, 'asks for the calculator by its USB ID (1209:0001)');
  check((await page.textContent('#files')).includes('Geen bestanden'), 'connected: empty calculator listed');

  const small = Buffer.from("print('hallo')\n");
  await page.setInputFiles('#pick', { name: 'mijn script.py', mimeType: 'text/plain', buffer: small });
  await page.waitForSelector('tr[data-name="mijn_script.py"]');
  check((await page.textContent('#status')).includes('verstuurd'), 'upload: "mijn script.py" sent as mijn_script.py');

  const big = crypto.randomBytes(100 * 1024);
  await page.setInputFiles('#pick', { name: 'big.bin', mimeType: 'application/octet-stream', buffer: big });
  await page.waitForSelector('tr[data-name="big.bin"]', { timeout: 60000 });
  check((await page.textContent('tr[data-name="big.bin"] .size')).trim() === '100.0 KB', '100 KB file uploaded, listed as 100.0 KB');

  await page.click('tr[data-name="big.bin"] .get');
  await page.waitForFunction(() => window.lastDownload && window.lastDownload.name === 'big.bin', null, { timeout: 60000 });
  const got = Buffer.from(await page.evaluate(() => { let s = ''; const d = window.lastDownload.data; for (let i = 0; i < d.length; i++) s += String.fromCharCode(d[i]); return btoa(s); }), 'base64');
  check(got.equals(big), 'download returns the exact 100 KB');

  await page.setInputFiles('#pick', { name: 'other.bin', mimeType: 'application/octet-stream', buffer: crypto.randomBytes(100 * 1024) });
  await page.waitForSelector('#status.err', { timeout: 60000 });
  check((await page.textContent('#status')).includes('no_space'), "a second 100 KB file: the calculator's ERR no_space is shown");
  check(await page.$('tr[data-name="other.bin"]') === null && await page.$('tr[data-name="big.bin"]') !== null, 'nothing half-written, big.bin still there');

  await page.setInputFiles('#pick', { name: 'huge.bin', mimeType: 'application/octet-stream', buffer: Buffer.alloc(100 * 1024 + 1) });
  await page.waitForFunction(() => document.querySelector('#status').textContent.includes('groter dan 100 KB'));
  check(true, 'over 100 KB refused before sending');
  await page.setInputFiles('#pick', { name: 'a_name_that_is_far_too_long.py', mimeType: 'text/plain', buffer: small });
  await page.waitForFunction(() => document.querySelector('#status').textContent.includes('te lang'));
  check(true, 'names over 23 characters refused');

  await page.click('tr[data-name="big.bin"] .del');
  await page.waitForFunction(() => !document.querySelector('tr[data-name="big.bin"]'));
  check((await page.textContent('#status')).includes('gewist'), 'delete (after confirming): gone from the list');

  await page.click('#lang');
  check(await page.textContent('h1') === 'NumWorks OS files' && (await page.textContent('tr[data-name="mijn_script.py"] .get')) === 'Download', 'switch to English');
  await page.click('#connect');
  check((await page.textContent('#status')).includes('Not connected'), 'disconnect');

  await browser.close();
  dev.kill();
  console.log(`${fails ? 'FAILED' : 'ALL PASSED'} (${fails} failure${fails === 1 ? '' : 's'})`);
  process.exit(fails ? 1 : 0);
})().catch(e => { console.log('  FAIL', e.message); process.exit(1); });
