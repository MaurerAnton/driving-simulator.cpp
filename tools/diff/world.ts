// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The map layer of the original, printed as numbers – to be compared with what
 * tools/dev/world_dump.cpp prints from the port (tools/diff/compare.py).
 *
 *   cd /home/user/driving-simulator
 *   node /path/to/port/tools/diff/world.ts [--map public/map] [--part all|net|tiles] > original.txt
 *
 * (run inside the original, whose modules it loads; `--root` names it otherwise)
 *
 * Every line is a name followed by values.  Whole numbers (counts, ids) must
 * be the same in both outputs, fractions agree to a relative 1e-9.  The order
 * of everything – roads, lanes, paths through junctions – is part of what is
 * compared: lanes and paths get their numbers in the order they are made.
 *
 * What both tools do, step by step, is written once here; world_dump.cpp
 * follows it line for line.
 */
import { resolve } from 'node:path';

const args = process.argv.slice(2);
const arg = (name: string, dflt: string): string => {
  const i = args.indexOf(`--${name}`);
  return i >= 0 && i + 1 < args.length ? args[i + 1] : dflt;
};
const root = resolve(arg('root', process.cwd()));
const mapDir = resolve(root, arg('map', 'public/map'));
const part = arg('part', 'all');
const src = (path: string): string => `${root}/src/${path}`;

const { samplePolyline, newPolySample, ringArea, hash01, rng } = await import(src('core/geom.ts'));
const { MapLoader } = await import(src('world/loader.ts'));
const { LaneGraph, lanePoint } = await import(src('world/lanes.ts'));
const { NetIndex } = await import(src('world/netindex.ts'));
const { netPolyline } = await import(src('world/netfile.ts'));
const { quickest, quickestToEdge } = await import(src('world/routegraph.ts'));
const { fingerprint } = await import(src('world/netdiff.ts'));
const { unpackTile, tileGround } = await import(src('world/tilefmt.ts'));
const { parseFar, farHeight, farBuilt } = await import(src('world/far.ts'));
const { GridSet, gridIndex, TILE } = await import(src('world/grid.ts'));
const { calmLift } = await import(src('world/roadfeatures.ts'));
const { diskFiles } = await import(`${root}/tools/mapgen/mapfiles.ts`);
const { setWasmMode } = await import(src('wasm/kernels.ts'));
if (arg('wasm', 'auto') === 'off') setWasmMode('off');

// ---------------------------------------------------------------------------
// printing

const lines: string[] = [];
const fmt = (v: unknown): string => {
  if (typeof v === 'number') return Object.is(v, -0) ? '0' : String(v);
  if (typeof v === 'boolean') return v ? '1' : '0';
  if (v === null || v === undefined) return '-';
  return String(v).replace(/\s+/g, '_') || '""';
};
const out = (name: string, ...values: unknown[]): void => {
  lines.push(values.length ? `${name} ${values.map(fmt).join(' ')}` : name);
};
const flush = (): void => {
  process.stdout.write(lines.join('\n') + '\n');
  lines.length = 0;
};

/** a number for a list of numbers that changes when one of them does, or their order: Σ v[i] · (i mod 13 + 1) */
const sumOf = (a: ArrayLike<number>): number => {
  let s = 0;
  for (let i = 0; i < a.length; i++) s += a[i] * ((i % 13) + 1);
  return s;
};
/** the same for a stride-3 polyline, taken around its first point (small numbers: nothing cancels) */
const lineSum = (p: ArrayLike<number>): number => {
  let s = 0;
  for (let i = 0; i + 2 < p.length; i += 3) s += ((p[i] - p[0]) + 2 * (p[i + 1] - p[1]) + 3 * (p[i + 2] - p[2])) * (((i / 3) % 13) + 1);
  return s;
};

// ---------------------------------------------------------------------------
// the map

const files = diskFiles(mapDir);
const map = await MapLoader.open(files);
const world = map.world;
const lanes = new LaneGraph(world);
// (as src/sim/follow.ts keeps the lanes in step with the network)
map.listen({
  added: (c: { edges: unknown[]; nodes: unknown[] }) => {
    for (const e of c.edges) lanes.addEdge(e);
    for (const n of c.nodes) lanes.completeNode(n);
  },
  removed: (c: { edges: unknown[]; nodes: unknown[] }) => {
    for (const n of c.nodes) lanes.removeNode(n);
    for (const e of c.edges) lanes.removeEdge(e);
  },
});

const index = JSON.parse(new TextDecoder().decode((await files.bytes('map.json')) as ArrayBuffer));
out('map', map.version, map.pieces, map.tiles.x0, map.tiles.z0, map.tiles.size, world.meta.name, world.meta.minX, world.meta.maxX, world.meta.minZ, world.meta.maxZ, world.meta.originE, world.meta.originN);
out('index', index.nodes, index.edges, GridSet.from(index.tiles).count, GridSet.from(index.chunks).count, world.json.places.length, world.json.spawns.length);
out('yard', world.json.yard?.x, world.json.yard?.z, world.json.yard?.len, world.json.yard?.wid, world.json.yard?.rot);
{
  let s = 0;
  for (const p of world.json.spawns) s += p.x + 2 * p.z + 3 * p.yaw + (p.rough ? 1 : 0) + p.name.length + (p.g ?? '').length + p.kind.length;
  for (const p of world.json.places) s += p.x - p.z + p.name.length + p.kind.length;
  out('places', s);
}

const net = await map.net();
const netIndex = new NetIndex(net);

/** where things are looked at: Baunatal, the middle of Kassel, a point on a motorway */
const places: Array<[string, number, number]> = [
  ['Baunatal', 0, 0],
  ['Kassel', 6500, -7600],
];
{
  let e = 0;
  while (e < net.edgeA.length && !(net.edgeCls[e] === 0 && net.edgeLen[e] > 500)) e++;
  places.push(['motorway', net.nodeX[net.edgeA[e]], net.nodeZ[net.edgeA[e]]]);
}

const counts = (what: string): void => {
  let stubs = 0;
  for (const n of world.allNodes) if (n.stub) stubs++;
  let laneCount = 0;
  for (let i = 0; i < lanes.lanes.length; i++) if (lanes.lanes[i]) laneCount++;
  let signals = 0;
  for (let i = 0; i < world.signals.length; i++) if (world.signals[i]) signals++;
  out('counts', what, world.allNodes.size, stubs, world.allEdges.size, laneCount, lanes.connectors.length, world.crossings.length, signals, world.nodes.length, world.edges.length, lanes.lanes.length);
};

const tmp = newPolySample();
const hit = { y: 0, edge: null as null | { id: number }, node: null as null | { id: number }, s: 0, off: 0, zone: 2, surf: 6 };
const foot = { x: 0, z: 0, d: 0 };

function dumpNetwork(name: string, px: number, pz: number): void {
  out('place', name, px, pz, map.ready(px, pz), map.progress(px, pz));
  counts('at');

  // --- every road: sums over all of them (in the order of their numbers), every 25th in full
  {
    const sum = { len: 0, width: 0, margin: 0, kerb: 0, cut: 0, blend: 0, box: 0, speed: 0, flags: 0, lanes: 0, pts: 0, xings: 0, calm: 0, narrow: 0, turns: 0, side: 0 };
    for (let id = 0; id < world.edges.length; id++) {
      const e = world.edges[id];
      if (!e) continue;
      sum.len += e.len;
      sum.width += e.width + e.laneW + e.swL + 2 * e.swR;
      sum.margin += e.margin + 2 * e.marginL + 3 * e.marginR;
      sum.kerb += e.kerbL + 2 * e.kerbR;
      sum.cut += e.cutA + 2 * e.cutB;
      sum.blend += e.blendA + e.blendB;
      sum.box += e.maxX - e.minX + (e.maxZ - e.minZ);
      sum.speed += e.maxF + 2 * e.maxB + 3 * e.zone;
      sum.flags += e.flags + e.access * 4096 + e.surf * 16384 + e.cls * 65536 + e.median + e.mouth + e.layer * 7 + e.tier;
      sum.lanes += e.lanesF + 3 * e.lanesB;
      sum.pts += e.pl.cum.length + lineSum(e.pl.pts);
      sum.xings += e.xings.length;
      for (const x of e.xings) sum.xings += x.s + x.k * 3 + x.w + (x.isl ? 1 : 0) + (x.sc ?? -1) + (x.g ?? -1) + x.e;
      if (e.calm) sum.calm += e.calm.length + sumOf(e.calm) + calmLift(e, e.len / 2);
      if (e.narrow) for (const n of e.narrow) sum.narrow += 1 + n.s0 + n.s1 + n.at + n.side + n.depth + n.lo + n.hi + (n.single ? 1 : 0) + n.flags;
      sum.turns += e.turnF.length + 2 * e.turnB.length + e.name.length + e.ref.length;
      sum.side += (e.xl ? 1 + Object.keys(e.xl).length : 0) + (e.xr ? 100 + Object.keys(e.xr).length : 0);
    }
    out('edgesum', sum.len, sum.width, sum.margin, sum.kerb, sum.cut, sum.blend, sum.box, sum.speed, sum.flags, sum.lanes, sum.pts, sum.xings, sum.calm, sum.narrow, sum.turns, sum.side);
    for (let id = 0; id < world.edges.length; id += 25) {
      const e = world.edges[id];
      if (!e) continue;
      const p = e.pl.pts;
      const n = e.pl.cum.length;
      out('edge', id, e.cls, e.len, e.width, e.lanesF, e.lanesB, e.laneW, e.maxF, e.maxB, e.zone, e.surf, e.flags, e.access, e.cutA, e.cutB, e.layer, e.margin, e.marginL, e.marginR, e.median, e.kerbL, e.kerbR, e.mouth, e.swL, e.swR, e.tier, e.blendA, e.blendB, e.a.id, e.b.id, e.osm, n, p[0], p[1], p[2], p[n * 3 - 3], p[n * 3 - 2], p[n * 3 - 1], lineSum(p), e.minX, e.maxX, e.minZ, e.maxZ, e.xings.length);
    }
  }

  // --- every lane: sums, and every 25th in full
  {
    const sum = { off: 0, len: 0, stop: 0, limit: 0, links: 0, ends: 0 };
    for (let id = 0; id < lanes.lanes.length; id++) {
      const l = lanes.lanes[id];
      if (!l) continue;
      sum.off += l.off + l.width;
      sum.len += l.len + l.s0 + 2 * l.s1;
      sum.stop += l.stopAt;
      sum.limit += Number.isFinite(l.limit) ? l.limit : 1000;
      sum.links += l.out.length + 3 * l.in.length + l.idx + 5 * l.count + l.dir;
      sum.ends += l.edge.id + 2 * l.from.id + 3 * l.to.id + l.armTo.idx + 7 * l.armFrom.idx;
    }
    out('lanesum', sum.off, sum.len, sum.stop, sum.limit, sum.links, sum.ends);
    for (let id = 0; id < lanes.lanes.length; id += 25) {
      const l = lanes.lanes[id];
      if (!l) continue;
      lanePoint(l, l.len / 2, tmp);
      out('lane', id, l.edge.id, l.dir, l.idx, l.count, l.off, l.width, l.s0, l.s1, l.len, l.from.id, l.to.id, l.armTo.edge.id, l.armFrom.edge.id, l.limit, l.stopAt, l.out.length, l.in.length, tmp.x, tmp.y, tmp.z, tmp.tx, tmp.tz);
    }
  }

  // --- the paths through the junctions: sums over all, and every 10th junction in full
  lanes.resolveAll();
  {
    const sum = { len: 0, turn: 0, pts: 0, conflicts: 0, at: 0, yields: 0, merges: 0, siblings: 0, until: 0 };
    for (const c of lanes.connectors) {
      sum.len += c.len;
      sum.turn += c.turn + 1;
      sum.pts += c.pl.cum.length + lineSum(c.pl.pts);
      sum.conflicts += c.conflicts.length;
      for (const k of c.conflicts) {
        sum.at += k.sSelf + 2 * k.sOther;
        if (k.yield) sum.yields++;
        if (k.merge) sum.merges++;
      }
      sum.siblings += c.siblings.length;
      for (const s of c.siblings) sum.until += s.until + 2 * s.untilOther;
    }
    out('pathsum', sum.len, sum.turn, sum.pts, sum.conflicts, sum.at, sum.yields, sum.merges, sum.siblings, sum.until);
    for (let id = 0; id < world.nodes.length; id += 10) {
      const n = world.nodes[id];
      if (!n) continue;
      const sh = n.shape;
      out('node', id, n.x, n.y, n.z, n.gx, n.gz, n.ctrl, n.sc, n.dark, n.stub, n.tc, n.osm, n.arms.length, n.fr.length, sumOf(n.fr), sh ? sh.ring.length : -1, sh ? ringArea(sh.ring) : 0, sh ? sh.corners.length : 0, sh ? sh.minX : 0, sh ? sh.maxX : 0, sh ? sh.minZ : 0, sh ? sh.maxZ : 0);
      if (sh) {
        const r = sh.ring;
        let s = 0;
        for (let i = 0; i < r.length; i += 2) s += ((r[i] - n.x) + 2 * (r[i + 1] - n.z)) * (((i / 2) % 13) + 1);
        let k = 0;
        for (const c of sh.corners) k += c.kerb.length + c.swA + 2 * c.swB + c.arc0 + 3 * c.arc1 + (c.ox - n.x) * (c.r > 0 ? 1 : 0) + (c.oz - n.z) * (c.r > 0 ? 1 : 0) + c.r;
        out('shape', id, s, k);
      }
      for (const a of n.arms) out('arm', id, a.idx, a.edge.id, a.out, a.role, a.sg, a.lamp, a.stopS, a.cut, a.dx, a.dz, a.ang, a.half, a.swL, a.swR, a.isApproach, a.isExit);
      if (n.stub) continue;
      for (const c of lanes.pathsThrough(n)) {
        out('path', c.id, c.from.id, c.to.id, c.turn, c.len, c.pl.cum.length, lineSum(c.pl.pts), c.conflicts.length, c.siblings.length);
        for (const k of c.conflicts) out('conflict', c.id, k.other.id, k.sSelf, k.sOther, k.merge, k.yield);
        for (const s of c.siblings) out('sibling', c.id, s.other.id, s.until, s.untilOther);
      }
    }
  }

  // --- the surface: points on and beside every 7th road
  {
    for (let id = 0; id < world.edges.length; id += 7) {
      const e = world.edges[id];
      if (!e) continue;
      for (const at of [0.03, 0.5, 0.97]) {
        samplePolyline(e.pl, e.len * at, tmp);
        // left of the direction (tx, tz) is (tz, -tx)
        for (const side of [0, e.width / 2 - 0.3, -(e.width / 2 + 0.8), e.width / 2 + e.swL + 2.5]) {
          const x = tmp.x + tmp.tz * side;
          const z = tmp.z - tmp.tx * side;
          const found = world.surfaceAt(x, z, tmp.y + 0.6, hit);
          out('surface', id, at, found, found ? hit.y : 0, hit.edge ? hit.edge.id : -1, hit.node ? hit.node.id : -1, found ? hit.s : 0, found ? hit.off : 0, hit.zone, found ? hit.surf : 0);
        }
        const x = tmp.x + tmp.tz * 1.1;
        const z = tmp.z - tmp.tx * 1.1;
        const located = world.locate(x, z, tmp.y, 6, hit);
        out('locate', id, at, located, located ? hit.edge!.id : -1, located ? hit.s : 0, located ? hit.off : 0, located ? hit.y : 0, located ? hit.zone : 0);
        const near = world.nearestRoad(tmp.x + tmp.tz * (e.width / 2 + 9), tmp.z - tmp.tx * (e.width / 2 + 9), 30, 0, hit);
        out('nearest', id, at, near, near ? hit.edge!.id : -1, near ? hit.s : 0, near ? hit.off : 0, near ? hit.y : 0, near ? hit.zone : 0);
        const d = lanes.nearestPath(x, z, tmp.y, 4, foot);
        out('nearpath', id, at, d, Number.isFinite(d) ? foot.x : 0, Number.isFinite(d) ? foot.z : 0);
        const post = lanes.besideLanes(x, z, NaN, 1.4, foot);
        out('beside', id, at, post !== null, post ? foot.x : 0, post ? foot.z : 0, post ? foot.d : 0);
        out('wall', id, at, lanes.wallInLane(x - 3, z - 2, x + 3, z + 2, tmp.y, 0.8), lanes.wallInLane(x + tmp.tz * 30, z - tmp.tx * 30, x + tmp.tz * 34, z - tmp.tx * 34, NaN, 0.8));
      }
    }
    let inBox = 0;
    let segs = 0;
    let nodesIn = 0;
    world.edgesInBox(px - 300, pz - 300, px + 300, pz + 300, (e: { id: number }) => {
      inBox += e.id + 1;
    });
    world.segmentsInBox(px - 300, pz - 300, px + 300, pz + 300, (e: { id: number }, seg: number) => {
      segs += e.id * 3 + seg + 1;
    });
    world.nodesInBox(px - 300, pz - 300, px + 300, pz + 300, (n: { id: number }) => {
      nodesIn += n.id + 1;
    });
    out('inbox', inBox, segs, nodesIn);
  }

  // --- ways through the lanes that are loaded: from the lanes of the lowest roads to the highest
  {
    const ids: number[] = [];
    for (let id = 0; id < world.edges.length; id++) if (world.edges[id]) ids.push(id);
    const pick = (k: number): { id: number } => world.edges[ids[Math.floor((ids.length - 1) * k)]];
    const pairs: Array<[number, number, boolean, number]> = [
      [0.1, 0.9, false, 0],
      [0.3, 0.6, true, 0],
      [0.5, 0.2, false, 1],
      [0.8, 0.4, true, -1],
    ];
    for (const [from, to, forTraffic, goalDir] of pairs) {
      const el = lanes.edgeLanes[pick(from).id];
      const start = el.fwd[0] ?? el.back[0];
      const goal = pick(to);
      if (!start) {
        out('laneroute', pick(from).id, goal.id, -2);
        continue;
      }
      const r = lanes.route(start, goal, forTraffic, goalDir);
      let len = 0;
      const first: number[] = [];
      for (const c of r ?? []) {
        len += c.len + c.to.len;
        if (first.length < 10) first.push(c.id);
      }
      out('laneroute', pick(from).id, goal.id, r ? r.length : -1, len, ...first);
    }
  }
  flush();
}

if (part === 'all' || part === 'net') {
  for (const [name, x, z] of places) {
    await map.loadAround(x, z);
    // (what lies far behind goes, one chunk with each call)
    for (let i = 0; i < 60; i++) map.update(x, z, Infinity);
    dumpNetwork(name, x, z);
    // six kilometres on: chunks are added in front and dropped behind
    await map.loadAround(x + 6000, z);
    for (let i = 0; i < 60; i++) map.update(x + 6000, z, Infinity);
    counts('moved');
    out('ready', map.ready(x + 6000, z), map.ready(x, z), map.progress(x, z, 9000));
    flush();
  }
}

// ---------------------------------------------------------------------------
// the network of the whole map: ways, and what is where

if (part === 'all' || part === 'net') {
  const g = netIndex.graph;
  out('net', net.nodeX.length, net.edgeA.length, net.to.length, net.poly.length, g.n, fingerprint(net));
  out('netsum', sumOf(net.nodeX), sumOf(net.nodeZ), sumOf(net.edgeLen), sumOf(net.edgeCls), sumOf(net.wayKind), sumOf(net.waySpeed), sumOf(net.turn), sumOf(net.across));
  out('graphsum', sumOf(g.len), sumOf(g.secs), sumOf(g.across), sumOf(g.x0), sumOf(g.z1), sumOf(g.firstIn), sumOf(g.from), sumOf(g.inTurn), sumOf(netIndex.main));
  const spots: Array<[number, number]> = [
    [0, 0],
    [6500, -7600],
    [places[2][1], places[2][2]],
    [-40000, 30000],
    [40000, -30000],
    [20000, 45000],
    [-55000, -20000],
    [744, -272],
  ];
  const usable = (e: number): boolean => netIndex.main[e * 2] === 1 || netIndex.main[e * 2 + 1] === 1;
  const wayAt: number[] = [];
  for (const [x, z] of spots) {
    const any = netIndex.nearest(x, z, 500);
    const spot = netIndex.nearest(x, z, 30000, usable);
    out('spot', x, z, any ? any.edge : -1, any ? any.d : 0, spot ? spot.edge : -1, spot ? spot.x : 0, spot ? spot.z : 0, spot ? spot.yaw : 0, spot ? spot.d : 0);
    wayAt.push(spot ? spot.edge * 2 + (netIndex.main[spot.edge * 2] === 1 ? 0 : 1) : -1);
    let n = 0;
    let s = 0;
    netIndex.edgesInBox(x - 1500, z - 800, x + 1500, z + 800, (e: number) => {
      n++;
      s += e;
    });
    let nb = 0;
    let sb = 0;
    netIndex.edgesInBox(x - 5000, z - 5000, x + 5000, z + 5000, (e: number) => {
      nb++;
      sb += e;
    }, true);
    out('box', n, s, nb, sb);
    if (spot) {
      const p = netPolyline(net, spot.edge);
      let ps = 0;
      for (let i = 0; i < p.length; i += 2) ps += ((p[i] - p[0]) + 2 * (p[i + 1] - p[1])) * (i / 2 + 1);
      const path = { n: 0, s: 0, moveTo(px: number, pz: number): void { this.n++; this.s += px - 2 * pz; }, lineTo(px: number, pz: number): void { this.n++; this.s += px + 3 * pz; } };
      netIndex.trace(spot.edge, path);
      out('course', spot.edge, p.length, ps, path.n, path.s);
    }
  }
  const pairs: Array<[number, number]> = [
    [0, 1],
    [1, 2],
    [3, 4],
    [5, 6],
    [7, 3],
    [2, 5],
  ];
  for (const [a, b] of pairs) {
    const start = wayAt[a];
    const goal = wayAt[b];
    const ways = start >= 0 && goal >= 0 ? quickest(g, start, goal) : null;
    let secs = 0;
    let len = 0;
    for (const w of ways ?? []) {
      secs += g.secs[w];
      len += g.len[w];
    }
    out('route', start, goal, ways ? ways.length : -1, secs, len, ...(ways ?? []).slice(0, 10).map((w: number) => w >> 1));
    const toEdge = start >= 0 && goal >= 0 ? quickestToEdge(g, start, goal >> 1) : null;
    out('routeto', toEdge ? toEdge.length : -1, toEdge && toEdge.length ? toEdge[toEdge.length - 1] : -1);
  }
  flush();
}

// ---------------------------------------------------------------------------
// tiles, and the far view

if (part === 'all' || part === 'tiles') {
  const tileAt: Array<[number, number]> = [
    [0, 0],
    [6500, -7600],
    [places[2][1], places[2][2]],
    [688.22, -294.77],
  ];
  for (const [x, z] of tileAt) {
    const ix = gridIndex(x, TILE);
    const iz = gridIndex(z, TILE);
    const buf = await files.bytes(`tiles/${ix}_${iz}.fsb`);
    out('tile', ix, iz, map.tiles.has(ix, iz), buf !== null);
    if (!buf) continue;
    const tile = unpackTile(buf);
    const h = tile.header;
    out('header', h.v, h.ix, h.iz, h.cx, h.cz, h.yBase, h.size, h.terrainN, h.terrainMin, h.terrainScale, h.coverN, h.signs.length, h.objects.length, h.clr ?? -1, Object.keys(h.sections).length);
    for (const [name, info] of Object.entries(h.sections) as Array<[string, { n: number; t: string; c?: number; s?: number }]>) {
      const a = tile.get(name);
      out('section', name, info.t, info.n, info.c ?? 0, info.s ?? 1, a.length, sumOf(a));
    }
    for (const name of ['bldg_a16', 'trees', 'collPts', 'nothing']) {
      const a = tile.get(name);
      out('derived', name, a ? a.length : -1, a ? sumOf(a) : 0);
    }
    const hgt = tile.get('hgt');
    for (const [dx, dz] of [[0, 0], [123.4, -56.7], [-249.99, 249.99], [250, -250], [17.3, 201.9]]) out('ground', tileGround(h, hgt, h.cx + dx, h.cz + dz));
    let s = 0;
    for (const p of h.signs) s += p.x + 2 * p.y + 3 * p.z + p.yaw + p.h + (p.big ?? 0) + p.s.length + p.s.join('').length;
    for (const o of h.objects) s += o.x + 2 * o.y + 3 * o.z + (o.yaw ?? 0) + o.k.length + (o.p ?? '').length;
    out('things', s);
  }
  const farBuf = await map.far();
  const far = farBuf ? parseFar(farBuf) : null;
  out('far', far !== null, far ? far.minX : 0, far ? far.minZ : 0, far ? far.cell : 0, far ? far.w : 0, far ? far.h : 0, far ? far.base : 0, far ? far.step : 0, far ? sumOf(far.heights) : 0, far ? sumOf(far.cover) : 0, far && far.buildings ? sumOf(far.buildings) : -1);
  if (far) {
    for (const [x, z] of [[0, 0], [6500, -7600], [-64999, -45999], [12345.6, 23456.7], [58999, 59999], [1e7, 0]]) out('farat', farHeight(far, x, z), farBuilt(far, x, z));
  }
  // (small things the map layer leans on)
  const r = rng(20261124);
  out('rng', r(), r(), r(), hash01(0), hash01(1), hash01(-5), hash01(123456789), hash01(4294967296 + 7));
  flush();
}
