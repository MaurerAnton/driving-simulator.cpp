// SPDX-License-Identifier: AGPL-3.0-only
// A picture of web/data/outline.bin with the regions' boxes on top – to see that the
// outline lies where the regions do (not mirrored, not shifted, no holes):
//
//   node tools/dev/outline-preview.mjs [--out /tmp/outline.svg] [--in web/data/outline.bin]
//        [--regions URL or file of regions.json]
//        [--view minX,minZ,maxX,maxZ]   km of the world frame (default: the outline's whole frame)
//        [--far]                        the far level instead of the near one
//        [--seams]                      also the lines that are not drawn (the frame's edge), in magenta
//        [--width 900]                  pixels
//
// It also says which country each region belongs to, as the game will find it, and where
// that is not the country the region's id names.  (rsvg-convert -o x.png x.svg makes a PNG.)
import { readFileSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { KIND, PLACES, arcPoints, latLonToWorld, readOutline, ringPoints } from '../build-outline.mjs';

const here = dirname(fileURLToPath(import.meta.url));
const arg = (name, def) => {
  const i = process.argv.indexOf(name);
  return i > 0 ? process.argv[i + 1] : def;
};
const far = process.argv.includes('--far');
const target = resolve(arg('--out', '/tmp/outline.svg'));
const regionsFrom = arg('--regions', 'https://raw.githubusercontent.com/MaurerAnton/driving-simulator/main/map-assets/regions.json');
const width = Number(arg('--width', '900'));

const o = readOutline(readFileSync(resolve(arg('--in', join(here, '../../web/data/outline.bin')))));
const regions = (/^https?:/.test(regionsFrom) ? await (await fetch(regionsFrom)).json() : JSON.parse(readFileSync(regionsFrom, 'utf8'))).regions;
/** the site's own map (North Hesse), which regions.json does not list */
const SITE_MAP = [-65000, -46000, 59000, 60000];

const view = arg('--view', '') ? arg('--view', '').split(',').map((km) => Number(km) * 1000) : o.box.map((b) => b * o.unit);
const scale = width / (view[2] - view[0]);
const height = Math.round((view[3] - view[1]) * scale);
const px = (x) => ((x - view[0]) * scale).toFixed(1);
const pz = (z) => ((z - view[1]) * scale).toFixed(1);
/** an SVG path of points in steps */
const path = (pts, close) => {
  let d = '';
  for (let i = 0; i < pts.length; i += 2) d += `${i ? 'L' : 'M'}${px(pts[i] * o.unit)} ${pz(pts[i + 1] * o.unit)}`;
  return d + (close ? 'Z' : '');
};
const visible = (r) => !far || r.far;
const esc = (t) => t.replace(/&/g, '&amp;').replace(/</g, '&lt;');

const svg = [];
svg.push(`<svg xmlns="http://www.w3.org/2000/svg" width="${width}" height="${height}" viewBox="0 0 ${width} ${height}" font-family="sans-serif">`);
svg.push(`<rect width="${width}" height="${height}" fill="#bcd6e6"/>`);
// the land: every country one path, filled non-zero (which leaves its holes out)
o.countries.forEach((c, i) => {
  const d = c.rings.filter(visible).map((r) => path(ringPoints(o, r, far), true)).join('');
  svg.push(`<path d="${d}" fill="${['#eee9d8', '#e6e0cc', '#f3efe2', '#ded8c2'][i % 4]}" fill-rule="nonzero"/>`);
});
svg.push(`<path d="${o.lakes.flatMap((l) => l.rings.filter(visible).map((r) => path(ringPoints(o, r, far), true))).join('')}" fill="#bcd6e6" fill-rule="nonzero"/>`);
// the lines, each once
const line = (kind) => o.arcs.filter((a) => a.kind === kind && (!far || a.far)).map((a) => path(arcPoints(a, far), false)).join('');
svg.push(`<path d="${line(KIND.shore)}" fill="none" stroke="#7fa8c4" stroke-width="0.6"/>`);
svg.push(`<path d="${line(KIND.coast)}" fill="none" stroke="#5b87a6" stroke-width="0.8"/>`);
svg.push(`<path d="${line(KIND.border)}" fill="none" stroke="#a0522d" stroke-width="0.8"/>`);
if (process.argv.includes('--seams')) svg.push(`<path d="${line(KIND.seam)}" fill="none" stroke="#f0f" stroke-width="0.8"/>`);
// the regions
const box = (b, colour) => `<rect x="${px(b[0])}" y="${pz(b[1])}" width="${((b[2] - b[0]) * scale).toFixed(1)}" height="${((b[3] - b[1]) * scale).toFixed(1)}" fill="${colour}" fill-opacity="0.25" stroke="${colour}" stroke-width="0.7"/>`;
svg.push(box(SITE_MAP, '#1a7f37'));
for (const r of regions) svg.push(box(r.box, '#d1242f'));
// the countries' names where the file says a name can stand, and a few places (through the game's projection)
for (const c of o.countries) svg.push(`<text x="${px(c.label[0] * o.unit)}" y="${pz(c.label[1] * o.unit)}" font-size="11" text-anchor="middle" fill="#555">${esc(c.nameDe)}</text>`);
for (const [name, lat, lon] of PLACES) {
  const [x, z] = latLonToWorld(lat, lon);
  svg.push(`<circle cx="${px(x)}" cy="${pz(z)}" r="2.2" fill="#000"/><text x="${px(x)}" y="${pz(z)}" dx="4" dy="3" font-size="9">${esc(name)}</text>`);
}
svg.push('</svg>');
writeFileSync(target, svg.join('\n'));
console.log(`${target}: ${width} × ${height} px, ${((view[2] - view[0]) / width).toFixed(0)} m a pixel, the ${far ? 'far' : 'near'} level`);

// Which country a point lies in, and which one a box belongs to – as countryAt and
// countryOfBox of src/ui/worldoutline.cpp find it.
const windings = (pts, x, z) => {
  let w = 0;
  for (let i = 0, n = pts.length; i < n; i += 2) {
    const ax = pts[i] * o.unit;
    const az = pts[i + 1] * o.unit;
    const bx = pts[(i + 2) % n] * o.unit;
    const bz = pts[(i + 3) % n] * o.unit;
    const side = (bx - ax) * (z - az) - (bz - az) * (x - ax);
    if (az <= z && bz > z && side > 0) w++;
    if (az > z && bz <= z && side < 0) w--;
  }
  return w;
};
const rings = o.countries.map((c) => c.rings.map((r) => ringPoints(o, r, false)));
const countryAt = (x, z) => o.countries.find((_, i) => rings[i].reduce((w, pts) => w + windings(pts, x, z), 0) !== 0) ?? null;
const countryOfBox = (b) => {
  const at = (u, v) => countryAt(b[0] + u * (b[2] - b[0]), b[1] + v * (b[3] - b[1]));
  const middle = at(0.5, 0.5);
  if (middle) return middle;
  const points = new Map();
  let most = null;
  for (let i = 0; i < 5; i++) {
    for (let j = 0; j < 5; j++) {
      const c = at((i + 0.5) / 5, (j + 0.5) / 5);
      if (!c) continue;
      points.set(c, (points.get(c) ?? 0) + 1);
      if (!most || points.get(c) > points.get(most)) most = c;
    }
  }
  return most;
};
// the regions' ids say where they are ("at-graz", "norwegen-bergen"; no such beginning: Germany)
const said = (id) => ({ at: 'AT', be: 'BE', ch: 'CH', fr: 'FR', it: 'IT', norwegen: 'NO' })[id.split('-')[0]] ?? 'DE';
const code = (c) => (c ? c.iso : 'no country');
const byCountry = new Map();
const inWater = [];
const odd = [];
for (const r of [{ id: '(the site)', box: SITE_MAP }, ...regions]) {
  const found = code(countryOfBox(r.box));
  byCountry.set(found, (byCountry.get(found) ?? 0) + 1);
  if (!countryAt((r.box[0] + r.box[2]) / 2, (r.box[1] + r.box[3]) / 2)) inWater.push(r.id);
  if (found !== said(r.id)) odd.push(`${r.id} (${found})`);
}
console.log(`${regions.length + 1} regions (with the site's own map) by country: ${[...byCountry].map(([k, n]) => `${k} ${n}`).join(', ')}`);
if (inWater.length) console.log(`  the middle of the box is water (the country is then the one most of the box lies in): ${inWater.join(', ')}`);
if (odd.length) console.log(`  not the country the region's id names: ${odd.join(', ')}`);
for (const [name, lat, lon, iso] of PLACES) {
  const c = countryAt(...latLonToWorld(lat, lon));
  if ((c ? c.iso : '') !== iso) console.log(`${name} lies in ${code(c)}, not in ${iso || 'none'}`);
}
