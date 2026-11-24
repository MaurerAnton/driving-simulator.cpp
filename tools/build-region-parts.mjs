// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Which parts really hold each region's tiles: `web/parts/regions.json`.
 *
 * The regions' index of map-assets (regions.json) names, for each region, the parts
 * (N.tar) its tiles are in.  For a few regions that list is wrong – Köln's names
 * part 62, whose tiles lie elsewhere; its own are in part 32 – and the region then
 * opens with no ground at all.  This script compares every region's grid of tiles
 * (its map.json) with the tiles each part holds (web/parts/N.json, made by
 * build-part-index.mjs) and writes the parts that hold them, for the regions where
 * that differs from the index.  The game reads it before it opens a region
 * (src/game/app.cpp).
 *
 *   node tools/build-region-parts.mjs [--from URL-of-map-assets/] [--parts web/parts]
 *
 * Prints what it corrected, and the regions whose tiles no part holds at all.
 */
import { readFileSync, readdirSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';

const args = process.argv.slice(2);
const arg = (name, otherwise) => {
  const i = args.indexOf(name);
  return i >= 0 && i + 1 < args.length ? args[i + 1] : otherwise;
};
const from = arg('--from', 'https://raw.githubusercontent.com/MaurerAnton/driving-simulator/main/map-assets/');
const partsDir = arg('--parts', 'web/parts');

// the tiles of every part
const owner = new Map(); // tile key → parts ("62.tar")
for (const f of readdirSync(partsDir)) {
  if (!/^\d+\.json$/.test(f)) continue;
  const idx = JSON.parse(readFileSync(join(partsDir, f), 'utf8'));
  const file = idx.file ?? f.replace(/\.json$/, '.tar');
  for (const key of Object.keys(idx.tiles ?? {})) {
    if (!owner.has(key)) owner.set(key, []);
    owner.get(key).push(file);
  }
}

/** the tile keys a map.json's grid names: { x0, z0, w, h, bits } (bits: base64, one bit a tile, row by row) */
function gridKeys(tiles) {
  const out = [];
  if (!tiles || typeof tiles.w !== 'number') return out;
  const bits = tiles.bits ? Buffer.from(tiles.bits, 'base64') : null;
  for (let j = 0; j < tiles.h; j++)
    for (let i = 0; i < tiles.w; i++) {
      const n = j * tiles.w + i;
      if (bits && !(bits[n >> 3] & (1 << (n & 7)))) continue;
      out.push(`${tiles.x0 + i}_${tiles.z0 + j}`);
    }
  return out;
}

const index = await (await fetch(`${from}regions.json`)).json();
const fixes = {};
const none = [];
let n = 0;
for (const r of index.regions) {
  n++;
  let map;
  try {
    const res = await fetch(`${from}${r.map ?? `regions/${r.id}/map.json`}`);
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    map = await res.json();
  } catch (e) {
    console.warn(`${r.id}: its map.json could not be read (${e.message})`);
    continue;
  }
  const keys = gridKeys(map.tiles);
  const count = new Map();
  for (const k of keys) for (const p of owner.get(k) ?? []) count.set(p, (count.get(p) ?? 0) + 1);
  const listed = r.tiles ?? [];
  const listedHold = listed.reduce((s, p) => s + (count.get(p) ?? 0), 0);
  if (keys.length > 0 && count.size === 0) {
    none.push(r.id);
    continue;
  }
  // the listed parts hold (nearly) all: nothing to correct
  if (listedHold >= keys.length * 0.9) continue;
  const parts = [...count.entries()].sort((a, b) => b[1] - a[1]).map(([p]) => p);
  fixes[r.id] = parts;
  console.log(`${r.id}: listed ${listed.join(' ')} (holding ${listedHold} of ${keys.length} tiles) → ${parts.join(' ')}`);
}
writeFileSync(join(partsDir, 'regions.json'), `${JSON.stringify({ from, fixes, missing: none })}\n`);
console.log(`${n} regions; ${Object.keys(fixes).length} corrected; tiles in no part: ${none.join(' ') || 'none'}`);
