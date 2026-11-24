// SPDX-License-Identifier: AGPL-3.0-only
// A picture of the browser build, headless:
//   node tools/dev/shot.mjs "<query string>" out.png [width] [height] [extra wait ms]
// Needs tools/dev/serve.mjs running (FS_URL, default http://127.0.0.1:5180/) and Playwright's Chromium
// (taken from the original repository's node_modules: FS_ORIGINAL, default /home/user/driving-simulator).
// FS_MOBILE=1: a phone (touch, no mouse).  FS_STEPS: "key:KeyW:800,click:.btn,wait:500,eval:<js>" done in turn after the game is ready.
// FS_GPU=1 draws with the machine's graphics chip (when the browser can reach it without a window).
// Prints what the page wrote to the console, and window.__fs.info() at the end.
import { existsSync, readdirSync } from 'node:fs';
import { createRequire } from 'node:module';
import { homedir } from 'node:os';
import { join } from 'node:path';

const original = process.env.FS_ORIGINAL ?? '/home/user/driving-simulator';
const { chromium } = createRequire(join(original, 'package.json'))('playwright-core');

function findChrome() {
  if (process.env.CHROME_BIN) return process.env.CHROME_BIN;
  const cache = join(homedir(), '.cache', 'ms-playwright');
  if (existsSync(cache)) {
    for (const d of readdirSync(cache).filter((d) => /^chromium-\d+$/.test(d)).sort().reverse()) {
      for (const sub of readdirSync(join(cache, d))) {
        const p = join(cache, d, sub, 'chrome');
        if (existsSync(p)) return p;
      }
    }
  }
  throw new Error('No Chromium found. Set CHROME_BIN or run: npx playwright-core install chromium');
}

const [query = '', out = 'shot.png', w = '1280', h = '720', extra = '500'] = process.argv.slice(2);
const base = process.env.FS_URL ?? 'http://127.0.0.1:5180/';
const browser = await chromium.launch({
  executablePath: findChrome(),
  headless: true,
  // FS_GPU=1: the machine's own graphics chip instead of the software renderer (for timings); FS_ARGS: other flags
  args: process.env.FS_ARGS
    ? process.env.FS_ARGS.split(' ')
    : process.env.FS_GPU
      ? ['--use-gl=angle', '--use-angle=gles-egl', '--enable-gpu', '--ignore-gpu-blocklist', '--enable-webgl', '--no-sandbox', '--ozone-platform=headless']
      : ['--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist', '--enable-webgl', '--no-sandbox'],
});
const mobile = !!process.env.FS_MOBILE;
const page = await browser.newPage({ viewport: { width: Number(w), height: Number(h) }, deviceScaleFactor: 1, isMobile: mobile, hasTouch: mobile });
const logs = [];
page.on('console', (m) => logs.push(`[${m.type()}] ${m.text()}`));
page.on('pageerror', (e) => logs.push(`[pageerror] ${e.message}`));
page.on('response', (r) => {
  if (r.status() >= 400) logs.push(`[http ${r.status()}] ${r.url()}`);
});
page.on('requestfailed', (r) => logs.push(`[failed] ${r.url()} ${r.failure()?.errorText ?? ''}`));
const t0 = Date.now();
await page.goto(base + (query ? `?${query}` : ''), { waitUntil: 'domcontentloaded', timeout: 240000 });
try {
  await page.waitForFunction(() => window.__fs && window.__fs.ready === true, null, { timeout: Number(process.env.FS_TIMEOUT ?? 180000) });
} catch (e) {
  logs.push(`[shot] not ready: ${e.message.split('\n')[0]}`);
}
const ready = Date.now() - t0;
for (const step of (process.env.FS_STEPS ?? '').split(',').filter(Boolean)) {
  const [kind, a, b] = step.split(':');
  try {
    if (kind === 'key') {
      await page.keyboard.down(a);
      await page.waitForTimeout(Number(b ?? 100));
      await page.keyboard.up(a);
    } else if (kind === 'click') await page.click(a, { timeout: 5000 });
    else if (kind === 'tap') await page.tap(a, { timeout: 5000 });
    else if (kind === 'wait') await page.waitForTimeout(Number(a));
    else if (kind === 'eval') logs.push(`[eval] ${JSON.stringify(await page.evaluate(step.slice(5)))}`);
  } catch (e) {
    logs.push(`[shot] step ${step}: ${e.message.split('\n')[0]}`);
  }
}
await page.waitForTimeout(Number(extra));
await page.screenshot({ path: out });
let info = null;
try {
  info = await page.evaluate(() => (window.__fs && window.__fs.info ? window.__fs.info() : null));
} catch (e) {
  logs.push(`[shot] info: ${e.message.split('\n')[0]}`);
}
console.log(logs.join('\n'));
console.log(`ready after ${ready} ms; ${out} written` + (info ? `\ninfo: ${JSON.stringify(info)}` : ''));
await browser.close();
