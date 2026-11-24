// SPDX-License-Identifier: AGPL-3.0-only
// A small web server for trying the browser build on this machine:
//   node tools/dev/serve.mjs [site folder] [port] [map folder]
// serves the site (default build-web/site) and, under /map/, the map (default the original's public/map),
// with byte ranges (the regional maps' parts are read that way) – nothing is cached by the browser.
import { createReadStream, existsSync, statSync } from 'node:fs';
import { createServer } from 'node:http';
import { extname, join, normalize, resolve } from 'node:path';

const site = resolve(process.argv[2] ?? 'build-web/site');
const port = Number(process.argv[3] ?? 5180);
const map = resolve(process.argv[4] ?? '/home/user/driving-simulator/public/map');
const TYPES = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript', '.mjs': 'text/javascript', '.wasm': 'application/wasm', '.css': 'text/css', '.json': 'application/json', '.png': 'image/png', '.svg': 'image/svg+xml', '.gz': 'application/gzip' };

createServer((req, res) => {
  let path = decodeURIComponent(new URL(req.url, 'http://x').pathname);
  if (path.endsWith('/')) path += 'index.html';
  const file = path.startsWith('/map/') ? join(map, normalize(path.slice(5))) : join(site, normalize(path));
  if (!file.startsWith(site) && !file.startsWith(map)) return void res.writeHead(403).end();
  if (!existsSync(file) || !statSync(file).isFile()) return void res.writeHead(404, { 'content-type': 'text/plain' }).end('not found');
  const size = statSync(file).size;
  const head = { 'content-type': TYPES[extname(file)] ?? 'application/octet-stream', 'cache-control': 'no-store', 'accept-ranges': 'bytes', 'access-control-allow-origin': '*' };
  const m = /^bytes=(\d+)-(\d*)$/.exec(req.headers.range ?? '');
  if (m) {
    const a = Number(m[1]);
    const b = m[2] ? Math.min(Number(m[2]), size - 1) : size - 1;
    res.writeHead(206, { ...head, 'content-range': `bytes ${a}-${b}/${size}`, 'content-length': b - a + 1 });
    createReadStream(file, { start: a, end: b }).pipe(res);
  } else {
    res.writeHead(200, { ...head, 'content-length': size });
    createReadStream(file).pipe(res);
  }
}).listen(port, '127.0.0.1', () => console.log(`serving ${site} (and /map/ from ${map}) on http://127.0.0.1:${port}/`));
