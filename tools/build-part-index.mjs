// SPDX-License-Identifier: AGPL-3.0-only
// Where every tile lies inside the regional maps' parts.
//
// The regions' tiles are kept in the original repository as tar files of 20–100 MB
// (map-assets/N.tar: a manifest, then one gzipped tile per entry).  The 1.7.7 game
// downloads a whole part before it can show a region; this game asks for just the
// bytes of the tiles it needs (an HTTP range request) – for which it must know where
// each tile begins and how long it is.  This script finds that out once for every
// part and writes web/parts/N.json:
//
//   { "file": "62.tar", "bytes": 93327360, "type": "base", "codec": "gzip",
//     "tiles": { "20_386": [10752, 7069], … } }      (first byte, length)
//
//   node tools/build-part-index.mjs [--only 62,63] [--from URL-of-map-assets/] [--out web/parts]
//
// Parts whose index is already there are skipped (a part never changes once it is published).
// Each part is read as a stream and never stored: only the 512-byte headers are looked at.
import { existsSync, mkdirSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const arg = (name, def) => {
  const i = process.argv.indexOf(name);
  return i > 0 ? process.argv[i + 1] : def;
};
const from = arg('--from', 'https://raw.githubusercontent.com/MaurerAnton/driving-simulator/main/map-assets/').replace(/\/*$/, '/');
const out = resolve(arg('--out', join(here, '../web/parts')));
const only = arg('--only', '') ? new Set(arg('--only', '').split(',').map(Number)) : null;
mkdirSync(out, { recursive: true });

const index = await (await fetch(from + 'index.json')).json();
const files = index.files.filter((f) => !only || only.has(f.n));
console.log(`${index.files.length} parts in the index, ${files.length} to look at`);

const text = (b, a, n) => {
  let e = a;
  while (e < a + n && b[e] !== 0) e++;
  return new TextDecoder().decode(b.subarray(a, e));
};

/**
 * The entries of a tar that streams past: onEntry(name, where its data begins, how long it is).
 * Returns how long the archive is.  (The tiles' names are short: long-name and pax
 * headers are let go by like any other entry that is not a plain file.)
 */
async function scan(url, onEntry) {
  const res = await fetch(url);
  if (!res.ok || !res.body) throw new Error(`${url}: HTTP ${res.status}`);
  const reader = res.body.getReader();
  let pos = 0; // how many bytes of the archive have gone by
  let skip = 0; // bytes of an entry's data (and its padding) still to let go by
  const head = new Uint8Array(512);
  let have = 0;
  for (;;) {
    const { done, value } = await reader.read();
    if (done) break;
    let i = 0;
    while (i < value.length) {
      if (skip > 0) {
        const n = Math.min(skip, value.length - i);
        skip -= n;
        i += n;
        pos += n;
        continue;
      }
      const n = Math.min(512 - have, value.length - i);
      head.set(value.subarray(i, i + n), have);
      have += n;
      i += n;
      pos += n;
      if (have < 512) continue;
      have = 0;
      if (head.every((b) => b === 0)) continue; // the end blocks
      const size = parseInt(text(head, 124, 12).trim() || '0', 8);
      const type = String.fromCharCode(head[156] || 48);
      const prefix = text(head, 345, 155);
      const name = (prefix ? `${prefix}/` : '') + text(head, 0, 100);
      if (type === '0') onEntry(name.replace(/^\.\//, ''), pos, size);
      skip = Math.ceil(size / 512) * 512;
    }
  }
  return pos;
}

let made = 0;
for (const f of files) {
  const target = join(out, `${f.n}.json`);
  if (existsSync(target)) continue;
  const tiles = {};
  let others = 0;
  const t0 = Date.now();
  let bytes = 0;
  for (let attempt = 1; ; attempt++) {
    try {
      bytes = await scan(from + f.file, (name, offset, size) => {
        if (/^-?\d+_-?\d+$/.test(name)) tiles[name] = [offset, size];
        else others++;
      });
      break;
    } catch (e) {
      if (attempt >= 4) throw e;
      console.log(`  ${f.file}: ${e.message} – once more`);
      await new Promise((r) => setTimeout(r, 2000 * attempt));
    }
  }
  const listed = (f.tiles ?? []).length;
  const found = Object.keys(tiles).length;
  writeFileSync(target, JSON.stringify({ file: f.file, bytes, type: f.type ?? 'modified', codec: f.codec ?? 'gzip', tiles }));
  made++;
  console.log(`${f.file}: ${found} tiles${listed && listed !== found ? ` (the index lists ${listed})` : ''}, ${others} other entries, ${(bytes / 1e6).toFixed(1)} MB in ${((Date.now() - t0) / 1000).toFixed(1)} s`);
}
// what parts there are an index for (the game asks for this list first)
const have = index.files.filter((f) => existsSync(join(out, `${f.n}.json`))).map((f) => f.n);
writeFileSync(join(out, 'index.json'), JSON.stringify({ from, parts: have }));
console.log(`${made} written; ${have.length} of ${index.files.length} parts have an index`);
