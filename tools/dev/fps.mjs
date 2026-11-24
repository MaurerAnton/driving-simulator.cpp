// SPDX-License-Identifier: AGPL-3.0-only
// How many pictures a second, measured in a headless browser on this machine's graphics chip:
//   node tools/dev/fps.mjs <address with query> [seconds to measure, 12] [seconds to settle first, 15] [width] [height]
// Works for the original (https://maureranton.github.io/driving-simulator/?start=free) and for the port alike:
// it counts the page's animation frames, and reads window.__fs.info() (draw calls, triangles) where there is one.
import { takeBrowserLock } from './browserlock.mjs';
import { existsSync, readdirSync } from 'node:fs';
import { createRequire } from 'node:module';
import { homedir } from 'node:os';
import { join } from 'node:path';

const original = process.env.FS_ORIGINAL ?? '/home/user/driving-simulator';
const { chromium } = createRequire(join(original, 'package.json'))('playwright-core');
function findChrome() {
  if (process.env.CHROME_BIN) return process.env.CHROME_BIN;
  const cache = join(homedir(), '.cache', 'ms-playwright');
  for (const d of readdirSync(cache).filter((d) => /^chromium-\d+$/.test(d)).sort().reverse())
    for (const sub of readdirSync(join(cache, d))) if (existsSync(join(cache, d, sub, 'chrome'))) return join(cache, d, sub, 'chrome');
  throw new Error('No Chromium found');
}
const [url, secs = '12', settle = '15', w = '1280', h = '720'] = process.argv.slice(2);
await takeBrowserLock();
const browser = await chromium.launch({
  executablePath: findChrome(),
  headless: true,
  args: ['--use-gl=angle', '--use-angle=gles-egl', '--enable-gpu', '--ignore-gpu-blocklist', '--enable-webgl', '--no-sandbox', '--ozone-platform=headless', '--disable-gpu-vsync', '--disable-frame-rate-limit'],
});
const page = await browser.newPage({ viewport: { width: Number(w), height: Number(h) }, deviceScaleFactor: 1 });
const errors = [];
page.on('pageerror', (e) => errors.push(e.message));
await page.goto(url, { waitUntil: 'domcontentloaded', timeout: 240000 });
try {
  await page.waitForFunction(() => window.__fs && window.__fs.ready === true, null, { timeout: Number(process.env.FS_TIMEOUT ?? 240000) });
} catch (e) {
  console.log('not ready:', e.message.split('\n')[0]);
}
await page.waitForTimeout(Number(settle) * 1000);
const result = await page.evaluate(async (ms) => {
  const gl = document.createElement('canvas').getContext('webgl2');
  const ext = gl && gl.getExtension('WEBGL_debug_renderer_info');
  const chip = ext ? gl.getParameter(ext.UNMASKED_RENDERER_WEBGL) : '?';
  const times = [];
  let last = performance.now();
  const t0 = last;
  await new Promise((done) => {
    const tick = (now) => {
      times.push(now - last);
      last = now;
      if (now - t0 < ms) requestAnimationFrame(tick);
      else done();
    };
    requestAnimationFrame(tick);
  });
  times.sort((a, b) => a - b);
  const sum = times.reduce((a, b) => a + b, 0);
  let info = null;
  try {
    info = window.__fs && window.__fs.info ? window.__fs.info() : null;
  } catch (e) {
    info = String(e);
  }
  return { chip, frames: times.length, fps: (1000 * times.length) / sum, median: times[times.length >> 1], p95: times[Math.floor(times.length * 0.95)], worst: times[times.length - 1], info };
}, Number(secs) * 1000);
const i = result.info && typeof result.info === 'object' ? result.info : {};
console.log(`${result.chip}\n${result.fps.toFixed(1)} fps over ${result.frames} frames (median ${result.median.toFixed(1)} ms, 95 % under ${result.p95.toFixed(1)} ms, worst ${result.worst.toFixed(1)} ms)` +
  (i.calls !== undefined ? `; ${i.calls} draw calls, ${i.tris ?? i.triangles} triangles` : '') + (i.state ? `; state ${i.state}` : ''));
if (errors.length) console.log('page errors:', errors.slice(0, 3).join(' | '));
if (process.env.FS_SHOT) await page.screenshot({ path: process.env.FS_SHOT });
await browser.close();
