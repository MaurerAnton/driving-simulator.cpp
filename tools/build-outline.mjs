// SPDX-License-Identifier: AGPL-3.0-only
// The outlines of the countries, the coasts and the larger lakes around the game's regions:
// the background of the map that shows every region there is.
//
// Every region lies in one shared world frame (metres; origin = UTM zone 32 E 528000 /
// N 5678000; x grows east, z grows south: src/geo/utm), from about x −470 … +540 km and
// z −1400 … +940 km.  This script takes Natural Earth's 1:10m data (naturalearthdata.com,
// public domain; read from github.com/nvkelso/natural-earth-vector at a fixed version):
// the countries (ne_10m_admin_0_countries) and the lakes (ne_10m_lakes, ne_10m_lakes_europe),
// puts them through the game's projection, cuts them to a frame around the regions,
// thins the lines out and writes web/data/outline.bin.
//
// (Natural Earth is a small-scale map: at 1:10m its lines lie up to 2 km off the ground –
// the points where three countries meet around Germany by 0.6 … 2.2 km, most of them too
// far west.  The outline is a background for looking at many regions at once; where one
// region fills the screen, a coast is not where the region's own roads say it is.)
//
//   node tools/build-outline.mjs [--out web/data/outline.bin] [--cache build/natural-earth]
//        [--box -1800,-2600,1900,1800] the frame, in km (minX,minZ,maxX,maxZ): Europe from Ireland to Belarus, Norway to Tunisia
//        [--tolerance 300]             how far a line may be moved, in metres
//        [--far 2000]                  the same for the far level
//        [--lakes 30] [--far-lakes 150]  the smallest lake kept, in km²
//   node tools/build-outline.mjs --points    places through the projection (the numbers tests/worldoutline_test.cpp holds)
//
// How the lines are thinned out: the rings of all countries and lakes are split into arcs
// where they meet (a border belongs to both its countries: it is kept, and thinned, once),
// each arc by Douglas–Peucker.  The far level is the same recursion stopped earlier, so its
// points are some of the near level's.  Islands and lakes too small for a level are left
// out of it; where thinning made two lines cross, points are put back until they no longer do.
//
// THE FILE (src/ui/worldoutline.cpp reads it; `readOutline` below does the same for the tools)
//
//   Numbers are LEB128 varints (7 bits a byte, low bits first, the top bit: more follow);
//   signed ones (dx, dz …) are zigzag-coded first (0, −1, 1, −2 … → 0, 1, 2, 3 …).
//   Coordinates are steps of `unit` metres in the world frame.
//
//   "FSOL"                         four bytes
//   version                        one byte: 1
//   unit, tolerance, farTolerance  metres (50, 300, 2000)
//   minX, minZ, maxX, maxZ         signed: the frame everything was cut to
//   arcCount, then every arc:
//     kind                         0 coast · 1 border between two countries · 2 shore of a lake ·
//                                  3 a seam, not to be drawn (the frame's edge; where two lakes
//                                  are one water);  +4: the arc is in the far level too
//     n                            how many points (at least 2)
//     n × (dx, dz)                 signed: the step from the point before – for an arc's first point,
//                                  from the last point of the arc before it (the very first: from 0, 0)
//     (far arcs only) ⌈n/8⌉ bytes  bit i (the lowest bit of a byte first): point i is in the far level
//   countryCount, then every country:
//     2 bytes                      ISO 3166-1 alpha-2 ("DE")
//     length + UTF-8, twice        its name in English, then in German
//     labelX, labelZ               signed: a point well inside it (where its name can stand)
//     ringCount, then every ring:
//       flags                      1: a hole · 2: the ring is in the far level too
//       arcRefCount, then each     arc index × 2, + 1 when the arc is walked backwards
//   lakeCount, then every lake:    ringCount and rings as above
//
//   A ring is its arcs' points one after the other, without the point each arc shares with
//   the next (and the last with the first).  Rings run clockwise on a north-up map, holes the
//   other way round: one path of all rings of a country, filled non-zero, leaves the holes out.
//   An arc that is a closed line by itself (an island) repeats its first point at its end.
import { existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

// --- the game's projection -------------------------------------------------------
// ETRS89 / UTM zone 32N by Krüger's series, and the world frame: src/geo/utm.cpp
// (the original's src/geo/utm.ts) line by line.

const A_GRS80 = 6378137.0;
const F_GRS80 = 1 / 298.257222101;
const K0 = 0.9996;
const FALSE_EASTING = 500000;
const LON0_ZONE32 = (9 * Math.PI) / 180;
const N1 = F_GRS80 / (2 - F_GRS80);
const N2 = N1 * N1;
const N3 = N2 * N1;
const N4 = N2 * N2;
const A_RECT = (A_GRS80 / (1 + N1)) * (1 + N2 / 4 + N4 / 64);
const ALPHA = [N1 / 2 - (2 * N2) / 3 + (5 * N3) / 16, (13 * N2) / 48 - (3 * N3) / 5, (61 * N3) / 240];
const E_CONF = (2 * Math.sqrt(N1)) / (1 + N1);
const WORLD_ORIGIN_E = 528000;
const WORLD_ORIGIN_N = 5678000;

/** Geographic (degrees) -> world frame [x, z] in metres. */
export function latLonToWorld(latDeg, lonDeg) {
  const phi = (latDeg * Math.PI) / 180;
  const dLam = (lonDeg * Math.PI) / 180 - LON0_ZONE32;
  const sinPhi = Math.sin(phi);
  const t = Math.sinh(Math.atanh(sinPhi) - E_CONF * Math.atanh(E_CONF * sinPhi));
  const xiP = Math.atan2(t, Math.cos(dLam));
  const etaP = Math.atanh(Math.sin(dLam) / Math.sqrt(1 + t * t));
  let xi = xiP;
  let eta = etaP;
  for (let j = 0; j < 3; j++) {
    const k = 2 * (j + 1);
    xi += ALPHA[j] * Math.sin(k * xiP) * Math.cosh(k * etaP);
    eta += ALPHA[j] * Math.cos(k * xiP) * Math.sinh(k * etaP);
  }
  return [FALSE_EASTING + K0 * A_RECT * eta - WORLD_ORIGIN_E, -(K0 * A_RECT * xi - WORLD_ORIGIN_N)];
}

/** The places tests/worldoutline_test.cpp looks up: [name, latitude, longitude, the country's code or '']. */
export const PLACES = [
  ['Kassel', 51.3127, 9.4797, 'DE'],
  ['Berlin', 52.52, 13.405, 'DE'],
  ['Vienna', 48.2082, 16.3738, 'AT'],
  ['Zürich', 47.3769, 8.5417, 'CH'],
  ['Brussels', 50.8503, 4.3517, 'BE'],
  ['Paris', 48.8566, 2.3522, 'FR'],
  ['Nice', 43.7102, 7.262, 'FR'],
  ['Rome', 41.9028, 12.4964, 'IT'],
  ['Oslo', 59.9139, 10.7522, 'NO'],
  ['Bolzano', 46.4983, 11.3548, 'IT'],
  ['Innsbruck', 47.2692, 11.4041, 'AT'],
  ['Strasbourg', 48.5734, 7.7521, 'FR'],
  ['San Marino', 43.9424, 12.4578, 'SM'],
  ['Luxembourg', 49.6116, 6.1319, 'LU'],
  ['the North Sea', 56.0, 3.0, ''],
  ['the Adriatic', 43.5, 14.5, ''],
];

// --- the file ----------------------------------------------------------------------

export const KIND = { coast: 0, border: 1, shore: 2, seam: 3 };
const MAGIC = 'FSOL';
const VERSION = 1;

/**
 * The file's bytes from what it holds:
 *   { unit, tolerance, farTolerance, box: [minX, minZ, maxX, maxZ],
 *     arcs: [{ kind, far: bool, pts: [x0, z0, x1, z1 …], farMask: [bool …] }],
 *     countries: [{ iso, nameEn, nameDe, label: [x, z], rings: [{ hole, far, arcs: [[index, backwards] …] }] }],
 *     lakes: [{ rings }] }          (coordinates in steps of `unit`)
 */
function encodeOutline(o) {
  const out = [];
  const v = (n) => {
    if (!Number.isInteger(n) || n < 0 || n > 0x7fffffff) throw new Error(`not a number the file can hold: ${n}`);
    while (n >= 128) {
      out.push((n & 127) | 128);
      n >>>= 7;
    }
    out.push(n);
  };
  const s = (n) => v(n < 0 ? -2 * n - 1 : 2 * n);
  const text = (t) => {
    const b = new TextEncoder().encode(t);
    v(b.length);
    out.push(...b);
  };
  const rings = (list) => {
    v(list.length);
    for (const r of list) {
      v((r.hole ? 1 : 0) | (r.far ? 2 : 0));
      v(r.arcs.length);
      for (const [index, backwards] of r.arcs) v(index * 2 + (backwards ? 1 : 0));
    }
  };
  for (const c of MAGIC) out.push(c.charCodeAt(0));
  out.push(VERSION);
  v(o.unit);
  v(o.tolerance);
  v(o.farTolerance);
  for (const b of o.box) s(b);
  v(o.arcs.length);
  let px = 0;
  let pz = 0;
  for (const a of o.arcs) {
    const n = a.pts.length / 2;
    v(a.kind + (a.far ? 4 : 0));
    v(n);
    for (let i = 0; i < n; i++) {
      s(a.pts[2 * i] - px);
      s(a.pts[2 * i + 1] - pz);
      px = a.pts[2 * i];
      pz = a.pts[2 * i + 1];
    }
    if (a.far) {
      for (let i = 0; i < n; i += 8) {
        let byte = 0;
        for (let k = 0; k < 8 && i + k < n; k++) if (a.farMask[i + k]) byte |= 1 << k;
        out.push(byte);
      }
    }
  }
  v(o.countries.length);
  for (const c of o.countries) {
    if (!/^[A-Z]{2}$/.test(c.iso)) throw new Error(`not a country code: ${c.iso}`);
    out.push(c.iso.charCodeAt(0), c.iso.charCodeAt(1));
    text(c.nameEn);
    text(c.nameDe);
    s(c.label[0]);
    s(c.label[1]);
    rings(c.rings);
  }
  v(o.lakes.length);
  for (const l of o.lakes) rings(l.rings);
  return Uint8Array.from(out);
}

/** What a file holds (see encodeOutline); throws on anything that is not such a file. */
export function readOutline(bytes) {
  let p = 0;
  const fail = (why) => {
    throw new Error(`outline: ${why} (byte ${p} of ${bytes.length})`);
  };
  const u8 = () => (p < bytes.length ? bytes[p++] : fail('the file ends early'));
  const v = () => {
    let r = 0;
    for (let shift = 0; ; shift += 7) {
      const b = u8();
      if (shift > 28 || (shift === 28 && b > 7)) fail('a number is too large');
      r += (b & 127) * 2 ** shift;
      if (b < 128) return r;
    }
  };
  const s = () => {
    const n = v();
    return n % 2 ? -(n + 1) / 2 : n / 2;
  };
  const text = () => {
    const n = v();
    if (p + n > bytes.length) fail('the file ends early');
    p += n;
    return new TextDecoder('utf-8', { fatal: true }).decode(bytes.subarray(p - n, p));
  };
  for (const c of MAGIC) if (u8() !== c.charCodeAt(0)) fail('not an outline');
  if (u8() !== VERSION) fail('a version this reader does not know');
  const o = { unit: v(), tolerance: v(), farTolerance: v(), box: [s(), s(), s(), s()], arcs: [], countries: [], lakes: [] };
  if (o.unit < 1) fail('no unit');
  let x = 0;
  let z = 0;
  for (let count = v(); count > 0; count--) {
    const head = v();
    if (head > 7) fail('an arc of an unknown kind');
    const n = v();
    if (n < 2) fail('an arc without a line');
    const a = { kind: head & 3, far: (head & 4) !== 0, pts: [], farMask: [] };
    for (let i = 0; i < n; i++) {
      x += s();
      z += s();
      if (x < o.box[0] || x > o.box[2] || z < o.box[1] || z > o.box[3]) fail('a point outside the frame');
      a.pts.push(x, z);
    }
    if (a.far) {
      for (let i = 0; i < n; i += 8) {
        const byte = u8();
        for (let k = 0; k < 8 && i + k < n; k++) a.farMask.push((byte & (1 << k)) !== 0);
      }
      if (!a.farMask[0] || !a.farMask[n - 1]) fail("an arc's far level does not reach its ends");
    }
    o.arcs.push(a);
  }
  const uses = o.arcs.map(() => 0);
  const rings = () => {
    const list = [];
    for (let count = v(); count > 0; count--) {
      const flags = v();
      if (flags > 3) fail('a ring with unknown flags');
      const r = { hole: (flags & 1) !== 0, far: (flags & 2) !== 0, arcs: [] };
      for (let refs = v(); refs > 0; refs--) {
        const ref = v();
        if (ref >> 1 >= o.arcs.length) fail('a ring of an arc that is not there');
        // (a line has two sides: two countries share it, or two lakes)
        if (++uses[ref >> 1] > 4) fail('an arc in more rings than a line has sides');
        if (r.far && !o.arcs[ref >> 1].far) fail('a far ring of an arc that is not far');
        r.arcs.push([ref >> 1, (ref & 1) === 1]);
      }
      if (!r.arcs.length) fail('a ring without arcs');
      list.push(r);
    }
    return list;
  };
  for (let count = v(); count > 0; count--) {
    const iso = String.fromCharCode(u8(), u8());
    if (!/^[A-Z]{2}$/.test(iso)) fail('a country without a code');
    o.countries.push({ iso, nameEn: text(), nameDe: text(), label: [s(), s()], rings: rings() });
  }
  if (!o.countries.length) fail('no country');
  for (let count = v(); count > 0; count--) o.lakes.push({ rings: rings() });
  if (p !== bytes.length) fail('more bytes than the file should have');
  return o;
}

/** An arc's points of a level, in steps: [x0, z0, x1, z1 …]. */
export function arcPoints(arc, far = false) {
  if (!far) return arc.pts;
  const out = [];
  for (let i = 0; i < arc.farMask.length; i++) if (arc.farMask[i]) out.push(arc.pts[2 * i], arc.pts[2 * i + 1]);
  return out;
}

/** A ring's points of a level, in steps (closed without repeating the first); throws when its arcs do not join. */
export function ringPoints(o, ring, far = false) {
  const out = [];
  let end = null;
  for (const [index, backwards] of ring.arcs) {
    const pts = arcPoints(o.arcs[index], far);
    const n = pts.length / 2;
    const at = (i) => (backwards ? n - 1 - i : i);
    if (end && (end[0] !== pts[2 * at(0)] || end[1] !== pts[2 * at(0) + 1])) throw new Error('outline: the arcs of a ring do not join');
    for (let i = 0; i < n - 1; i++) out.push(pts[2 * at(i)], pts[2 * at(i) + 1]);
    end = [pts[2 * at(n - 1)], pts[2 * at(n - 1) + 1]];
  }
  if (end[0] !== out[0] || end[1] !== out[1]) throw new Error('outline: a ring does not close');
  return out;
}

// --- geometry ------------------------------------------------------------------------

/** Twice the area inside a ring of [x, z] points; positive when it runs clockwise on a north-up map (z points south). */
function area2(ring) {
  let a = 0;
  for (let i = 0, n = ring.length; i < n; i++) {
    const p = ring[i];
    const q = ring[(i + 1) % n];
    a += p[0] * q[1] - q[0] * p[1];
  }
  return a;
}

/** The same for a flat ring [x0, z0, x1, z1 …]. */
function flatArea2(r) {
  let a = 0;
  for (let i = 0, n = r.length; i < n; i += 2) {
    const j = (i + 2) % n;
    a += r[i] * r[j + 1] - r[j] * r[i + 1];
  }
  return a;
}

function pointInRing(x, z, ring) {
  let inside = false;
  for (let i = 0, n = ring.length, j = n - 1; i < n; j = i++) {
    const a = ring[i];
    const b = ring[j];
    if (a[1] > z !== b[1] > z && x < ((b[0] - a[0]) * (z - a[1])) / (b[1] - a[1]) + a[0]) inside = !inside;
  }
  return inside;
}

/**
 * Longitudes and latitudes the projection is at ease with: 60° either side of the
 * 9°E meridian, the northern half.  (Far from its meridian a transverse Mercator
 * map runs away – on the equator, 90° off, to infinity; the frame lies far inside this window.)
 */
const SAFE = [-51, 0, 69, 89];

/** A ring of [lon, lat] cut to the window (Sutherland–Hodgman: what it adds lies on the window's edge, far outside the frame). */
function cutToSafeWindow(ring) {
  if (ring.every(([lon, lat]) => lon >= SAFE[0] && lon <= SAFE[2] && lat >= SAFE[1] && lat <= SAFE[3])) return ring;
  let pts = ring;
  for (const [axis, limit, above] of [[0, SAFE[0], true], [0, SAFE[2], false], [1, SAFE[1], true], [1, SAFE[3], false]]) {
    const out = [];
    const keep = (p) => (above ? p[axis] >= limit : p[axis] <= limit);
    for (let i = 0; i < pts.length; i++) {
      const a = pts[i];
      const b = pts[(i + 1) % pts.length];
      if (keep(a)) out.push(a);
      if (keep(a) !== keep(b)) {
        const t = (limit - a[axis]) / (b[axis] - a[axis]);
        out.push(axis === 0 ? [limit, a[1] + t * (b[1] - a[1])] : [a[0] + t * (b[0] - a[0]), limit]);
      }
    }
    pts = out;
    if (pts.length < 3) return [];
  }
  return pts;
}

/**
 * The part of the segment a→b inside the box: [from, to, cut at its start, cut at its end], or null.
 * A cut point lies exactly on the box's edge; and the two ways of walking a segment give the
 * same points (two neighbours walk their common border in opposite directions).
 */
function clipSegment(a, b, box) {
  if (b[0] < a[0] || (b[0] === a[0] && b[1] < a[1])) {
    const r = clipSegment(b, a, box);
    return r && [r[1], r[0], r[3], r[2]];
  }
  const [minX, minZ, maxX, maxZ] = box;
  const dx = b[0] - a[0];
  const dz = b[1] - a[1];
  let t0 = 0;
  let t1 = 1;
  let e0 = -1;
  let e1 = -1;
  // Liang–Barsky: p·t ≤ q for each of the four edges
  const edge = (p, q, e) => {
    if (p === 0) return q >= 0;
    const t = q / p;
    if (p < 0) {
      if (t > t1) return false;
      if (t > t0) [t0, e0] = [t, e];
    } else {
      if (t < t0) return false;
      if (t < t1) [t1, e1] = [t, e];
    }
    return true;
  };
  if (!edge(-dx, a[0] - minX, 0) || !edge(dx, maxX - a[0], 1) || !edge(-dz, a[1] - minZ, 2) || !edge(dz, maxZ - a[1], 3)) return null;
  const at = (t, e) => {
    const p = [Math.min(maxX, Math.max(minX, a[0] + t * dx)), Math.min(maxZ, Math.max(minZ, a[1] + t * dz))];
    if (e < 2) p[0] = e === 0 ? minX : maxX;
    else p[1] = e === 2 ? minZ : maxZ;
    return p;
  };
  return [e0 < 0 ? a : at(t0, e0), e1 < 0 ? b : at(t1, e1), e0 >= 0, e1 >= 0];
}

/** Where a point of the box's edge lies along it: 0 … 4, the way a clockwise ring walks it (minX,minZ → maxX,minZ → maxX,maxZ → minX,maxZ). */
function alongEdge(p, box) {
  const [minX, minZ, maxX, maxZ] = box;
  if (p[1] === minZ && p[0] < maxX) return (p[0] - minX) / (maxX - minX);
  if (p[0] === maxX && p[1] < maxZ) return 1 + (p[1] - minZ) / (maxZ - minZ);
  if (p[1] === maxZ && p[0] > minX) return 2 + (maxX - p[0]) / (maxX - minX);
  if (p[0] === minX) return 3 + (maxZ - p[1]) / (maxZ - minZ);
  throw new Error(`clip: ${p} is not on the box's edge`);
}

/**
 * A polygon (rings of [x, z]; the first clockwise, its holes anticlockwise) cut to the box:
 * [{ pts, hole }].  The pieces a ring falls into are closed along the box's edge, each
 * into a ring of its own.
 */
function clipPolygon(rings, box) {
  const [minX, minZ, maxX, maxZ] = box;
  const inside = (p) => p[0] >= minX && p[0] <= maxX && p[1] >= minZ && p[1] <= maxZ;
  const out = [];
  const pieces = []; // what is inside of the rings that cross the edge: each piece comes in through the edge and leaves through it
  for (let r = 0; r < rings.length; r++) {
    const ring = rings[r];
    const n = ring.length;
    let start = 0;
    while (start < n && inside(ring[start])) start++;
    if (start === n) {
      out.push({ pts: ring, hole: r > 0 });
      continue;
    }
    let piece = null; // (the walk begins outside, so it ends outside)
    for (let k = 0; k < n; k++) {
      const c = clipSegment(ring[(start + k) % n], ring[(start + k + 1) % n], box);
      if (!c) continue;
      if (c[2] || !piece) piece = [c[0]];
      piece.push(c[1]);
      if (c[3]) {
        pieces.push(piece);
        piece = null;
      }
    }
    if (piece) throw new Error('clip: a ring that ends inside the box');
  }
  if (!pieces.length) {
    // nothing crosses the edge: is the box itself inside the polygon?
    const boxIn = rings.filter((ring) => pointInRing(minX, minZ, ring)).length % 2 === 1;
    if (boxIn) out.push({ pts: [[minX, minZ], [maxX, minZ], [maxX, maxZ], [minX, maxZ]], hole: false });
    return out;
  }
  // From where a piece leaves, the polygon's inside goes on along the edge (clockwise, as the
  // rings run) to where the next piece comes in.
  const ends = pieces.map((pts, i) => ({ i, tIn: alongEdge(pts[0], box), tOut: alongEdge(pts[pts.length - 1], box) }));
  const corner = [[minX, minZ], [maxX, minZ], [maxX, maxZ], [minX, maxZ]];
  const used = pieces.map(() => false);
  for (const first of ends) {
    if (used[first.i]) continue;
    const ring = [];
    for (let cur = first; ; ) {
      used[cur.i] = true;
      ring.push(...pieces[cur.i]);
      let next = null;
      let gap = Infinity;
      for (const e of ends) {
        const d = e.tIn >= cur.tOut ? e.tIn - cur.tOut : e.tIn - cur.tOut + 4;
        if (d < gap) [next, gap] = [e, d];
      }
      for (let c = Math.floor(cur.tOut) + 1; c <= cur.tOut + gap; c++) ring.push(corner[c % 4]);
      if (next === first) break;
      if (used[next.i]) throw new Error('clip: the pieces of a ring do not join up');
      cur = next;
    }
    out.push({ pts: ring, hole: false });
  }
  return out;
}

/** Distance (squared) of the point (px, pz) from the segment a–b. */
function segDist2(px, pz, ax, az, bx, bz) {
  const dx = bx - ax;
  const dz = bz - az;
  const len2 = dx * dx + dz * dz;
  let t = len2 === 0 ? 0 : ((px - ax) * dx + (pz - az) * dz) / len2;
  t = t < 0 ? 0 : t > 1 ? 1 : t;
  const ex = ax + t * dx - px;
  const ez = az + t * dz - pz;
  return ex * ex + ez * ez;
}

/**
 * Douglas–Peucker, all tolerances at once: for every point of the line [x0, z0, x1, z1 …] the
 * tolerance up to which it stays.  (The recursion splits a stretch at the point farthest from
 * its chord; that point stays while the tolerance is no more than that distance – and than
 * what the stretches around it stayed for, so a coarser line's points are always among a finer one's.)
 * The ends always stay; a closed line also keeps two more points, so a triangle is left of it at least.
 */
function significance(pts) {
  const n = pts.length / 2;
  const sig = new Float64Array(n);
  const fixed = [0, n - 1];
  if (pts[0] === pts[2 * n - 2] && pts[1] === pts[2 * n - 1]) {
    let far = 0;
    let best = -1;
    for (let i = 1; i < n - 1; i++) {
      const d = (pts[2 * i] - pts[0]) ** 2 + (pts[2 * i + 1] - pts[1]) ** 2;
      if (d > best) [far, best] = [i, d];
    }
    let side = 0;
    best = -1;
    for (let i = 1; i < n - 1; i++) {
      const d = segDist2(pts[2 * i], pts[2 * i + 1], pts[0], pts[1], pts[2 * far], pts[2 * far + 1]);
      if (i !== far && d > best) [side, best] = [i, d];
    }
    fixed.push(far);
    if (side) fixed.push(side);
    fixed.sort((a, b) => a - b);
  }
  const stack = [];
  for (let i = 0; i < fixed.length; i++) {
    sig[fixed[i]] = Infinity;
    if (i) stack.push([fixed[i - 1], fixed[i], Infinity]);
  }
  while (stack.length) {
    const [i, j, limit] = stack.pop();
    let k = -1;
    let best = -1;
    for (let m = i + 1; m < j; m++) {
      const d = segDist2(pts[2 * m], pts[2 * m + 1], pts[2 * i], pts[2 * i + 1], pts[2 * j], pts[2 * j + 1]);
      if (d > best) [k, best] = [m, d];
    }
    if (k < 0) continue;
    sig[k] = Math.min(Math.sqrt(best), limit);
    stack.push([i, k, sig[k]], [k, j, sig[k]]);
  }
  return sig;
}

const turn = (ax, az, bx, bz, cx, cz) => Math.sign((bx - ax) * (cz - az) - (bz - az) * (cx - ax));
const between = (ax, az, bx, bz, px, pz) => Math.min(ax, bx) <= px && px <= Math.max(ax, bx) && Math.min(az, bz) <= pz && pz <= Math.max(az, bz);

/** Do the segments a–b and c–d meet (touching counts)?  Whole numbers in, an exact answer out. */
function segmentsMeet(ax, az, bx, bz, cx, cz, dx, dz) {
  const o1 = turn(ax, az, bx, bz, cx, cz);
  const o2 = turn(ax, az, bx, bz, dx, dz);
  const o3 = turn(cx, cz, dx, dz, ax, az);
  const o4 = turn(cx, cz, dx, dz, bx, bz);
  if (o1 !== o2 && o3 !== o4) return true;
  return (
    (o1 === 0 && between(ax, az, bx, bz, cx, cz)) ||
    (o2 === 0 && between(ax, az, bx, bz, dx, dz)) ||
    (o3 === 0 && between(cx, cz, dx, dz, ax, az)) ||
    (o4 === 0 && between(cx, cz, dx, dz, bx, bz))
  );
}

/**
 * The point of a ring's inside that is farthest from its edge (the "pole of inaccessibility",
 * found as mapbox's polylabel finds it), to within `precision`: where a name can stand.
 */
function innerPoint(ring, precision) {
  let minX = Infinity;
  let minZ = Infinity;
  let maxX = -Infinity;
  let maxZ = -Infinity;
  for (let i = 0; i < ring.length; i += 2) {
    minX = Math.min(minX, ring[i]);
    maxX = Math.max(maxX, ring[i]);
    minZ = Math.min(minZ, ring[i + 1]);
    maxZ = Math.max(maxZ, ring[i + 1]);
  }
  // how far inside the point is (negative: outside)
  const depth = (x, z) => {
    let inside = false;
    let d2 = Infinity;
    for (let i = 0, n = ring.length, j = n - 2; i < n; j = i, i += 2) {
      const ax = ring[i];
      const az = ring[i + 1];
      const bx = ring[j];
      const bz = ring[j + 1];
      if (az > z !== bz > z && x < ((bx - ax) * (z - az)) / (bz - az) + ax) inside = !inside;
      d2 = Math.min(d2, segDist2(x, z, ax, az, bx, bz));
    }
    return (inside ? 1 : -1) * Math.sqrt(d2);
  };
  const cell = (x, z, h) => {
    const d = depth(x, z);
    return { x, z, h, d, most: d + h * Math.SQRT2 };
  };
  const size = Math.min(maxX - minX, maxZ - minZ);
  let best = cell((minX + maxX) / 2, (minZ + maxZ) / 2, 0);
  if (size <= 0) return [best.x, best.z];
  const queue = [];
  for (let x = minX; x < maxX; x += size) for (let z = minZ; z < maxZ; z += size) queue.push(cell(x + size / 2, z + size / 2, size / 2));
  while (queue.length) {
    queue.sort((a, b) => a.most - b.most);
    const c = queue.pop();
    if (c.d > best.d) best = c;
    if (c.most - best.d <= precision) continue;
    const h = c.h / 2;
    queue.push(cell(c.x - h, c.z - h, h), cell(c.x + h, c.z - h, h), cell(c.x - h, c.z + h, h), cell(c.x + h, c.z + h, h));
  }
  return [best.x, best.z];
}

// --- building ----------------------------------------------------------------------------

const NATURAL_EARTH = 'https://raw.githubusercontent.com/nvkelso/natural-earth-vector/v5.1.2/geojson/';

/** A file of Natural Earth, parsed; kept in `cache` once it has been fetched. */
async function naturalEarth(name, from, cache) {
  const path = join(cache, name);
  if (!existsSync(path)) {
    mkdirSync(cache, { recursive: true });
    for (let attempt = 1; ; attempt++) {
      try {
        const res = await fetch(from + name);
        if (!res.ok) throw new Error(`HTTP ${res.status}`);
        writeFileSync(path, Buffer.from(await res.arrayBuffer()));
        break;
      } catch (e) {
        if (attempt >= 4) throw new Error(`${from}${name}: ${e.message}`);
        console.log(`  ${name}: ${e.message} – once more`);
        await new Promise((r) => setTimeout(r, 2000 * attempt));
      }
    }
  }
  return JSON.parse(readFileSync(path, 'utf8'));
}

/** A feature's polygons: [[ring, hole …] …], rings of [lon, lat] (closed: the last point is the first again). */
function polygonsOf(feature) {
  const g = feature.geometry;
  if (!g) return [];
  if (g.type === 'Polygon') return [g.coordinates];
  if (g.type === 'MultiPolygon') return g.coordinates;
  throw new Error(`a geometry of type ${g.type}`);
}

/**
 * The outline: Natural Earth's countries and lakes in the world frame, cut to `box`
 * (metres), thinned out – as encodeOutline takes it.
 */
function buildOutline(countriesJson, lakesJson, opt, log) {
  const { unit, box } = opt;
  const stepBox = box.map((b) => b / unit);
  if (!stepBox.every(Number.isInteger)) throw new Error(`the frame is not in whole steps of ${unit} m`);
  const km2 = (unit * unit) / 1e6; // km² per square step

  // -- 1. projected, cut to the frame, in whole steps -------------------------------------
  /** A polygon of Natural Earth as rings in the frame: [{ pts: flat steps, hole }]. */
  const inFrame = (polygon) => {
    const rings = [];
    for (let r = 0; r < polygon.length; r++) {
      const lonLat = cutToSafeWindow(polygon[r].slice(0, -1));
      if (lonLat.length < 3) {
        if (r === 0) return [];
        continue;
      }
      const pts = lonLat.map(([lon, lat]) => latLonToWorld(lat, lon));
      // the first ring clockwise, the holes anticlockwise
      if (area2(pts) > 0 !== (r === 0)) pts.reverse();
      rings.push(pts);
    }
    const o = rings[0];
    if (o.every((p) => p[0] < box[0]) || o.every((p) => p[0] > box[2]) || o.every((p) => p[1] < box[1]) || o.every((p) => p[1] > box[3])) return [];
    const out = [];
    for (const ring of clipPolygon(rings, box)) {
      const pts = tidy(ring.pts.flatMap((p) => [Math.round(p[0] / unit), Math.round(p[1] / unit)]));
      if (pts.length >= 6 && flatArea2(pts) !== 0) {
        if (flatArea2(pts) > 0 === ring.hole) throw new Error('a ring that runs the wrong way round');
        out.push({ pts, hole: ring.hole });
      }
    }
    return out;
  };
  /** A ring in steps without points twice in a row and without spikes (there and straight back). */
  const tidy = (r) => {
    const out = [];
    for (let i = 0; i < r.length; i += 2) {
      out.push(r[i], r[i + 1]);
      for (;;) {
        const n = out.length;
        if (n >= 4 && out[n - 2] === out[n - 4] && out[n - 1] === out[n - 3]) out.length = n - 2;
        else if (n >= 6 && out[n - 2] === out[n - 6] && out[n - 1] === out[n - 5]) out.length = n - 4;
        else break;
      }
    }
    // … and round the ring's end
    for (;;) {
      const n = out.length;
      if (n >= 4 && out[0] === out[n - 2] && out[1] === out[n - 1]) out.length = n - 2;
      else if (n >= 6 && out[0] === out[n - 4] && out[1] === out[n - 3]) out.length = n - 2;
      else if (n >= 6 && out[2] === out[n - 2] && out[3] === out[n - 1]) out.splice(0, 2);
      else break;
    }
    return out;
  };

  const areaOf = (r) => (Math.abs(flatArea2(r.pts)) / 2) * km2;
  const rings = []; // every ring of the outline: { pts, hole, country | lake: index, area (km², of what it encloses) }
  // the countries that reach into the frame, in the order of their codes
  const countries = [];
  for (const f of countriesJson.features) {
    const p = f.properties;
    const found = polygonsOf(f).flatMap(inFrame);
    if (!found.length) continue;
    // (Natural Earth's ISO_A2 has no code for France and Norway, its ISO_A2_EH has)
    const iso = [p.ISO_A2_EH, p.ISO_A2].find((c) => /^[A-Z]{2}$/.test(c ?? ''));
    if (!iso) throw new Error(`no country code for ${p.NAME_EN ?? p.NAME}`);
    if (!p.NAME_EN || !p.NAME_DE) throw new Error(`no name for ${iso}`);
    if (countries.some((c) => c.iso === iso)) throw new Error(`two countries with the code ${iso}`);
    countries.push({ iso, nameEn: p.NAME_EN, nameDe: p.NAME_DE, found });
  }
  countries.sort((a, b) => (a.iso < b.iso ? -1 : 1));
  countries.forEach((c, index) => {
    for (const r of c.found) rings.push({ ...r, country: index, area: areaOf(r) });
    delete c.found;
  });
  // the lakes: of a lake that is in both files, the first one counts
  const lakes = [];
  const lakeEdges = [];
  for (const f of lakesJson.flatMap((j) => j.features)) {
    for (const polygon of polygonsOf(f)) {
      const found = inFrame(polygon);
      const outer = found.filter((r) => !r.hole).sort((a, b) => areaOf(b) - areaOf(a))[0];
      if (!outer || areaOf(outer) < opt.lakeMin) continue;
      const mid = innerPoint(outer.pts, 4);
      if (lakeEdges.some((other) => pointInRing(mid[0], mid[1], other))) continue;
      const edge = [];
      for (let i = 0; i < outer.pts.length; i += 2) edge.push([outer.pts[i], outer.pts[i + 1]]);
      lakeEdges.push(edge);
      for (const r of found) rings.push({ ...r, lake: lakes.length, area: areaOf(r) });
      lakes.push({});
    }
  }
  log(`${countries.length} countries and ${lakes.length} lakes in the frame: ${rings.length} rings, ${rings.reduce((n, r) => n + r.pts.length / 2, 0)} points`);

  // -- 2. arcs: the rings split where they meet ---------------------------------------------
  const key = (x, z) => (x + 0x20000) * 0x40000 + (z + 0x20000);
  const onEdge = (x, z) => x === stepBox[0] || x === stepBox[2] || z === stepBox[1] || z === stepBox[3];
  const alongFrame = (ax, az, bx, bz) => (ax === bx && (ax === stepBox[0] || ax === stepBox[2])) || (az === bz && (az === stepBox[1] || az === stepBox[3]));
  const neighbours = new Map(); // a point → the points it is joined to, in any ring
  const joints = new Set(); // the points where arcs end
  for (const ring of rings) {
    const r = ring.pts;
    const n = r.length;
    for (let i = 0; i < n; i += 2) {
      const k = key(r[i], r[i + 1]);
      let list = neighbours.get(k);
      if (!list) neighbours.set(k, (list = []));
      for (const j of [(i + n - 2) % n, (i + 2) % n]) {
        const other = key(r[j], r[j + 1]);
        if (!list.includes(other)) list.push(other);
        // where a line reaches the frame, the frame's edge begins: an arc of its own
        if (onEdge(r[i], r[i + 1]) && !alongFrame(r[i], r[i + 1], r[j], r[j + 1])) joints.add(k);
      }
    }
  }
  for (const [k, list] of neighbours) if (list.length > 2) joints.add(k);

  const arcs = []; // { pts, users: [ring index …], kind, closed, sig }
  const arcIndex = new Map();
  /** The arc with these points, in either direction: [index, backwards]. */
  const arcOf = (pts, ringIndex) => {
    const back = [];
    for (let i = pts.length - 2; i >= 0; i -= 2) back.push(pts[i], pts[i + 1]);
    const a = pts.join(',');
    const b = back.join(',');
    const backwards = b < a;
    const name = backwards ? b : a;
    let index = arcIndex.get(name);
    if (index === undefined) {
      index = arcs.length;
      arcIndex.set(name, index);
      arcs.push({ pts: backwards ? back : pts, users: [] });
    }
    arcs[index].users.push(ringIndex);
    return [index, backwards];
  };
  rings.forEach((ring, ringIndex) => {
    const r = ring.pts;
    const n = r.length / 2;
    const at = [];
    for (let i = 0; i < n; i++) if (joints.has(key(r[2 * i], r[2 * i + 1]))) at.push(i);
    ring.arcs = [];
    if (!at.length) {
      // a closed line by itself (an island; a country inside another): begun at its smallest point, so that both who walk it name the same arc
      let first = 0;
      for (let i = 1; i < n; i++) if (key(r[2 * i], r[2 * i + 1]) < key(r[2 * first], r[2 * first + 1])) first = i;
      at.push(first);
    }
    for (let a = 0; a < at.length; a++) {
      const from = at[a];
      const to = at[(a + 1) % at.length];
      const pts = [];
      for (let i = from; ; i = (i + 1) % n) {
        pts.push(r[2 * i], r[2 * i + 1]);
        if (i === to && pts.length > 2) break;
      }
      ring.arcs.push(arcOf(pts, ringIndex));
    }
  });
  let seams = 0;
  for (const a of arcs) {
    const p = a.pts;
    const n = p.length / 2;
    a.closed = p[0] === p[p.length - 2] && p[1] === p[p.length - 1];
    let frame = true;
    for (let i = 0; i + 1 < n; i++) if (!alongFrame(p[2 * i], p[2 * i + 1], p[2 * i + 2], p[2 * i + 3])) frame = false;
    const lands = a.users.filter((u) => rings[u].country !== undefined).map((u) => rings[u].country);
    a.land = lands.length > 0;
    const waters = a.users.filter((u) => rings[u].lake !== undefined).map((u) => rings[u].lake);
    if (frame) a.kind = KIND.seam;
    else if (lands.length >= 2) a.kind = new Set(lands).size > 1 ? KIND.border : KIND.seam;
    else if (lands.length === 1) a.kind = KIND.coast;
    else a.kind = waters.length >= 2 ? KIND.seam : KIND.shore;
    if (a.kind === KIND.seam && !frame) seams++;
    a.sig = significance(p);
  }
  log(`${arcs.length} arcs, ${arcs.reduce((n, a) => n + a.pts.length / 2, 0)} points (${arcs.filter((a) => a.kind === KIND.border).length} borders, ${arcs.filter((a) => a.kind === KIND.coast).length} coasts, ${arcs.filter((a) => a.kind === KIND.shore).length} shores, ${seams} seams inside an area)`);

  // -- 3. what each level holds ----------------------------------------------------------------
  // level 0 is the near one, level 1 the far one
  const tol = [opt.tolerance / unit, opt.farTolerance / unit];
  const smallest = tol.map((t) => (2 * t) ** 2 * km2); // an island is in a level when it is larger than this (km²)
  const lakeSmallest = [opt.lakeMin, opt.farLakeMin];
  const largest = countries.map((_, c) => rings.reduce((best, r, i) => (r.country === c && !r.hole && (best < 0 || r.area > rings[best].area) ? i : best), -1));
  const lakeOuter = lakes.map((_, l) => rings.reduce((best, r, i) => (r.lake === l && !r.hole && (best < 0 || r.area > rings[best].area) ? i : best), -1));
  /** Is the ring part of the level by its size? */
  const wanted = (i, level) => {
    const r = rings[i];
    if (r.lake !== undefined) {
      if (rings[lakeOuter[r.lake]].area < lakeSmallest[level]) return false;
      return !r.hole || r.area >= smallest[level];
    }
    const island = r.arcs.length === 1 && arcs[r.arcs[0][0]].closed;
    // (a country never loses its largest part in the near level, however small it is)
    return !island || r.area >= smallest[level] || (level === 0 && largest[r.country] === i);
  };
  for (const a of arcs) {
    // (a line two rings share is in a level when one of them wants it there: a country inside another, and the hole it lies in)
    a.level = [0, 1].map((level) => a.users.some((u) => wanted(u, level)));
    a.keep = [0, 1].map((level) => Array.from(a.sig, (sv) => sv >= tol[level]));
  }
  for (const r of rings) r.level = [0, 1].map((level) => r.arcs.every(([i]) => arcs[i].level[level]));

  // -- 4. no line may cross another ----------------------------------------------------------------
  /** The pairs of segments of a level that meet without being joined there: [[arc, from, to], [arc, from, to]] (from, to: points of the arc). */
  const meetings = (level) => {
    const segs = [];
    arcs.forEach((a, ai) => {
      if (!a.level[level]) return;
      let last = 0;
      for (let i = 1; i < a.keep[level].length; i++) {
        if (!a.keep[level][i]) continue;
        segs.push([ai, last, i]);
        last = i;
      }
    });
    const cell = 200; // steps
    const grid = new Map();
    const ends = (sg) => {
      const p = arcs[sg[0]].pts;
      return [p[2 * sg[1]], p[2 * sg[1] + 1], p[2 * sg[2]], p[2 * sg[2] + 1]];
    };
    segs.forEach((sg, si) => {
      const [ax, az, bx, bz] = ends(sg);
      for (let cx = Math.floor(Math.min(ax, bx) / cell); cx <= Math.floor(Math.max(ax, bx) / cell); cx++) {
        for (let cz = Math.floor(Math.min(az, bz) / cell); cz <= Math.floor(Math.max(az, bz) / cell); cz++) {
          const k = key(cx, cz);
          let list = grid.get(k);
          if (!list) grid.set(k, (list = []));
          list.push(si);
        }
      }
    });
    const found = new Map();
    for (const list of grid.values()) {
      for (let i = 0; i < list.length; i++) {
        const [ax, az, bx, bz] = ends(segs[list[i]]);
        for (let j = i + 1; j < list.length; j++) {
          const [cx, cz, dx, dz] = ends(segs[list[j]]);
          // (segments joined at an end are neighbours on a line, or arcs at their joint)
          if ((ax === cx && az === cz) || (ax === dx && az === dz) || (bx === cx && bz === cz) || (bx === dx && bz === dz)) continue;
          // (a border may well run through a lake: the lakes' shores only matter to each other)
          if (arcs[segs[list[i]][0]].land !== arcs[segs[list[j]][0]].land) continue;
          if (segmentsMeet(ax, az, bx, bz, cx, cz, dx, dz)) found.set(list[i] * segs.length + list[j], [segs[list[i]], segs[list[j]]]);
        }
      }
    }
    return [...found.values()];
  };
  /** Put back the point between two points of an arc that was dropped from the level last (the near level then has it too); false: none was dropped. */
  const putBack = (a, level, from, to) => {
    let best = -1;
    for (let i = from + 1; i < to; i++) if (!a.keep[level][i] && (best < 0 || a.sig[i] > a.sig[best])) best = i;
    if (best < 0) return false;
    a.keep[level][best] = true;
    a.keep[0][best] = true;
    return true;
  };
  /** Does all of the arc run along the frame's edge in this level? */
  const onFrame = (a, level) => {
    let last = 0;
    for (let i = 1; i < a.keep[level].length; i++) {
      if (!a.keep[level][i]) continue;
      if (!alongFrame(a.pts[2 * last], a.pts[2 * last + 1], a.pts[2 * i], a.pts[2 * i + 1])) return false;
      last = i;
    }
    return true;
  };
  const leftCrossing = [0, 0];
  const back = [0, 0];
  // the far level first: a point put back into it is in the near level too
  for (const level of [1, 0]) {
    // a coast thinned out until it runs along the frame's edge would be drawn where the map merely ends
    for (const a of arcs) while (a.kind !== KIND.seam && onFrame(a, level) && putBack(a, level, 0, a.sig.length - 1)) back[level]++;
    for (let round = 0; ; round++) {
      const pairs = meetings(level);
      let changed = 0;
      for (const sg of new Set(pairs.flat())) if (putBack(arcs[sg[0]], level, sg[1], sg[2])) changed++;
      back[level] += changed;
      if (!changed || round > 200) {
        leftCrossing[level] = pairs.length;
        break;
      }
    }
  }
  log(`lines that crossed after thinning (or lay on the frame's edge): ${back[0]} points put back in the near level, ${back[1]} in the far one`);
  if (leftCrossing[0] + leftCrossing[1]) log(`  (lines that meet in Natural Earth's own data, and so still do: ${leftCrossing[0]} in the near level, ${leftCrossing[1]} in the far one)`);

  // -- 5. the rings of each level; what is left of the arcs ------------------------------------------
  const ringAt = (r, level) => {
    const out = [];
    for (const [index, backwards] of r.arcs) {
      const a = arcs[index];
      const n = a.pts.length / 2;
      for (let i = 0; i < n - 1; i++) {
        const k = backwards ? n - 1 - i : i;
        if (a.keep[level][k]) out.push(a.pts[2 * k], a.pts[2 * k + 1]);
      }
    }
    return out;
  };
  const dropped = [0, 0];
  for (const r of rings) {
    for (const level of [0, 1]) {
      if (!r.level[level]) continue;
      // thinned out to nothing, or turned inside out: not in this level
      const pts = tidy(ringAt(r, level));
      if (pts.length < 6 || flatArea2(pts) > 0 === r.hole || flatArea2(pts) === 0) {
        r.level[level] = false;
        dropped[level]++;
      }
    }
    if (!r.level[0]) r.level[1] = false;
  }
  for (const a of arcs) a.level = [0, 1].map((level) => a.users.some((u) => rings[u].level[level]));
  if (dropped[0] + dropped[1]) log(`rings thinned out to nothing: ${dropped[0]} in the near level, ${dropped[1]} in the far one`);

  // -- 6. as the file holds it ------------------------------------------------------------------------
  const out = { unit, tolerance: opt.tolerance, farTolerance: opt.farTolerance, box: stepBox, arcs: [], countries: [], lakes: [] };
  const newIndex = new Map();
  const ringsOf = (mine) =>
    rings.filter((r) => mine(r) && r.level[0]).map((r) => ({
      hole: r.hole,
      far: r.level[1],
      arcs: r.arcs.map(([index, backwards]) => {
        if (!newIndex.has(index)) {
          const a = arcs[index];
          const at = a.keep[0].flatMap((k, i) => (k ? [i] : []));
          newIndex.set(index, out.arcs.length);
          out.arcs.push({ kind: a.kind, far: a.level[1], pts: at.flatMap((i) => [a.pts[2 * i], a.pts[2 * i + 1]]), farMask: a.level[1] ? at.map((i) => a.keep[1][i]) : [] });
        }
        return [newIndex.get(index), backwards];
      }),
    }));
  countries.forEach((c, ci) => {
    const list = ringsOf((r) => r.country === ci);
    if (!list.length) throw new Error(`nothing is left of ${c.nameEn}`);
    const main = tidy(ringAt(rings[largest[ci]], 0));
    out.countries.push({ ...c, label: innerPoint(main, 20).map(Math.round), rings: list });
  });
  lakes.forEach((_, li) => {
    const list = ringsOf((r) => r.lake === li);
    if (list.length) out.lakes.push({ rings: list });
  });
  return out;
}

/** What a file holds, in a few numbers. */
function summary(o) {
  const count = (far) => {
    let ringCount = 0;
    let points = 0;
    for (const area of [...o.countries, ...o.lakes]) {
      for (const r of area.rings) {
        if (far && !r.far) continue;
        ringCount++;
        points += ringPoints(o, r, far).length / 2;
      }
    }
    const arcList = o.arcs.filter((a) => !far || a.far);
    return { rings: ringCount, ringPoints: points, arcs: arcList.length, arcPoints: arcList.reduce((n, a) => n + arcPoints(a, far).length / 2, 0) };
  };
  return { countries: o.countries.length, lakes: o.lakes.length, near: count(false), far: count(true) };
}

async function main() {
  const here = dirname(fileURLToPath(import.meta.url));
  const arg = (name, def) => {
    const i = process.argv.indexOf(name);
    return i > 0 ? process.argv[i + 1] : def;
  };
  if (process.argv.includes('--points')) {
    for (const [name, lat, lon] of PLACES) {
      const [x, z] = latLonToWorld(lat, lon);
      console.log(`${name}: ${lat}, ${lon} -> ${x.toFixed(4)}, ${z.toFixed(4)}`);
    }
    return;
  }
  const from = arg('--from', NATURAL_EARTH).replace(/\/*$/, '/');
  const cache = resolve(arg('--cache', join(here, '../build/natural-earth')), from.replace(/\/geojson\/$/, '').split('/').pop());
  const target = resolve(arg('--out', join(here, '../web/data/outline.bin')));
  const opt = {
    unit: 50,
    box: arg('--box', '-1800,-2600,1900,1800').split(',').map((km) => Number(km) * 1000),
    tolerance: Number(arg('--tolerance', '300')),
    farTolerance: Number(arg('--far', '2000')),
    lakeMin: Number(arg('--lakes', '30')),
    farLakeMin: Number(arg('--far-lakes', '150')),
  };
  if (opt.box.length !== 4 || opt.box.some((b) => !Number.isFinite(b)) || opt.box[0] >= opt.box[2] || opt.box[1] >= opt.box[3]) throw new Error('--box minX,minZ,maxX,maxZ (km)');
  if (!Number.isInteger(opt.tolerance) || !Number.isInteger(opt.farTolerance) || opt.tolerance < 1 || opt.farTolerance < opt.tolerance) throw new Error('--tolerance and --far: whole metres, the far one the larger');
  if (!(opt.lakeMin >= 0) || !(opt.farLakeMin >= opt.lakeMin)) throw new Error('--lakes and --far-lakes: km², the far one the larger');

  const countries = await naturalEarth('ne_10m_admin_0_countries.geojson', from, cache);
  const lakes = [await naturalEarth('ne_10m_lakes.geojson', from, cache), await naturalEarth('ne_10m_lakes_europe.geojson', from, cache)];
  const outline = buildOutline(countries, lakes, opt, console.log);
  const bytes = encodeOutline(outline);

  // what was written must read back as what was meant
  const back = readOutline(bytes);
  if (JSON.stringify(back) !== JSON.stringify(outline)) throw new Error('the file does not read back as it was written');
  for (const area of [...back.countries, ...back.lakes]) {
    for (const r of area.rings) {
      for (const far of r.far ? [false, true] : [false]) {
        const pts = ringPoints(back, r, far);
        if (pts.length < 6 || flatArea2(pts) > 0 === r.hole) throw new Error('a ring that is none, or runs the wrong way round');
      }
    }
  }
  mkdirSync(dirname(target), { recursive: true });
  writeFileSync(target, bytes);
  const sum = summary(back);
  console.log(`${target}: ${bytes.length} bytes`);
  console.log(`  ${sum.countries} countries (${back.countries.map((c) => c.iso).join(' ')}), ${sum.lakes} lakes`);
  console.log(`  near (${opt.tolerance} m): ${sum.near.rings} rings of ${sum.near.ringPoints} points; ${sum.near.arcs} arcs of ${sum.near.arcPoints} points`);
  console.log(`  far (${opt.farTolerance} m): ${sum.far.rings} rings of ${sum.far.ringPoints} points; ${sum.far.arcs} arcs of ${sum.far.arcPoints} points`);
}

if (import.meta.main) await main();
