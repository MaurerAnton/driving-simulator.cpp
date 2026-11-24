// SPDX-License-Identifier: AGPL-3.0-only
/**
 * What the original makes of a small test town (tests/game/town.ts), printed
 * as numbers: the lines tests/world_test.cpp, tests/lanes_test.cpp,
 * tests/xsection_test.cpp and tests/netfile_test.cpp expect from the port (they print the same themselves
 * and compare with this output, kept under tests/fixtures).
 *
 *   cd /home/user/driving-simulator
 *   node /path/to/port/tools/diff/world_town.ts --part world   > tests/fixtures/world-town.txt
 *   node /path/to/port/tools/diff/world_town.ts --part lanes   > tests/fixtures/lanes-town.txt
 *   node /path/to/port/tools/diff/world_town.ts --part xsection > tests/fixtures/xsection-towns.json
 *   node /path/to/port/tools/diff/world_town.ts --part values   > tests/fixtures/xsection-values.txt
 *   node /path/to/port/tools/diff/world_town.ts --part net      > tests/fixtures/net-town.txt
 *
 * The last one writes the little towns of tests/world/xsection.test.ts as the
 * map build makes them from OpenStreetMap's tags (the port has no map build:
 * its test reads them).
 */
import { resolve } from 'node:path';

const args = process.argv.slice(2);
const arg = (name: string, dflt: string): string => {
  const i = args.indexOf(`--${name}`);
  return i >= 0 && i + 1 < args.length ? args[i + 1] : dflt;
};
const root = resolve(arg('root', process.cwd()));
const part = arg('part', 'world');
const src = (path: string): string => `${root}/src/${path}`;

const { makeTown } = await import(`${root}/tests/game/town.ts`);
const { samplePolyline, newPolySample, ringArea } = await import(src('core/geom.ts'));
const { XING } = await import(src('world/types.ts'));
const { World, computeAllCuts, edgePoints, packEdgePoints, computeJunctionCuts, buildJunctionShape } = await import(src('world/world.ts'));
const { LaneGraph, lanePoint } = await import(src('world/lanes.ts'));
const { routeGraphOf, quickest, quickestToEdge, mainWays, wayOf, routeFor } = await import(src('world/routegraph.ts'));

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
const sumOf = (a: ArrayLike<number>): number => {
  let s = 0;
  for (let i = 0; i < a.length; i++) s += a[i] * ((i % 13) + 1);
  return s;
};
const lineSum = (p: ArrayLike<number>): number => {
  let s = 0;
  for (let i = 0; i + 2 < p.length; i += 3) s += ((p[i] - p[0]) + 2 * (p[i + 1] - p[1]) + 3 * (p[i + 2] - p[2])) * (((i / 3) % 13) + 1);
  return s;
};

/** the town both tests look at: one-way and wide streets, lights, crossings, a forbidden turn, a cul-de-sac, pavements */
const townOptions = (lightsOut: boolean): object => ({
  cols: 4,
  rows: 3,
  pavement: 2,
  deadEnd: 60,
  wide: [[1, 1, 2, 1]],
  oneWay: [[0, 0, 1, 0]],
  lights: [[1, 1], [2, 1]],
  crossings: [
    { street: [2, 0, 3, 0], at: 0.5, k: XING.ZEBRA },
    { street: [1, 2, 2, 2], at: 0.4, k: XING.SIGNAL },
  ],
  bans: [[0, 1, 1, 1, 1, 0]],
  ...(lightsOut ? { lightsOut: (c: number) => c === 0 } : {}),
});

if (part === 'world') {
  for (const dark of [false, true]) {
    const town = makeTown(townOptions(dark));
    const world = town.world;
    out('town', dark, world.nodes.length, world.edges.length, world.allNodes.size, world.allEdges.size, world.signals.length, world.crossings.length);
    for (const e of world.edges) {
      out('edge', e.id, e.a.id, e.b.id, e.len, e.width, e.lanesF, e.lanesB, e.laneW, e.cutA, e.cutB, e.blendA, e.blendB, e.minX, e.maxX, e.minZ, e.maxZ, e.tier, e.swL, e.swR, e.kerbL, e.kerbR, e.marginL, e.marginR, e.xings.length, e.pl.cum.length, lineSum(e.pl.pts));
    }
    for (const n of world.nodes) {
      const sh = n.shape;
      out('node', n.id, n.x, n.z, n.ctrl, n.sc, n.dark, n.stub, n.arms.length, sumOf(n.fr), sh ? sh.ring.length : -1, sh ? ringArea(sh.ring) : 0, sh ? sh.corners.length : 0, sh ? sh.minX : 0, sh ? sh.maxX : 0, sh ? sh.minZ : 0, sh ? sh.maxZ : 0);
      for (const a of n.arms) out('arm', n.id, a.idx, a.edge.id, a.out, a.role, a.sg, a.lamp, a.stopS, a.cut, a.dx, a.dz, a.ang, a.half, a.swL, a.swR, a.isApproach, a.isExit);
      if (sh) for (const c of sh.corners) out('corner', n.id, c.kerb.length, sumOf(c.kerb), c.swA, c.swB, c.arc0, c.arc1, c.ox, c.oz, c.r);
    }
    for (const x of world.crossings) out('crossing', x.e, x.s, x.k, x.w, x.sc ?? -1, x.g ?? -1);
    if (dark) continue;
    // the surface: a grid of points over the town, at the height of the streets and well above a bridge that is not there
    const hit = { y: 0, edge: null as null | { id: number }, node: null as null | { id: number }, s: 0, off: 0, zone: 2, surf: 6 };
    for (let z = -20; z <= 220; z += 7.3) {
      for (let x = -20; x <= 380; x += 9.1) {
        const found = world.surfaceAt(x, z, 100.5, hit);
        if (found) out('surface', x, z, hit.y, hit.edge ? hit.edge.id : -1, hit.node ? hit.node.id : -1, hit.s, hit.off, hit.zone, hit.surf);
      }
    }
    out('below', world.surfaceAt(50, 0, 90, hit), world.surfaceAt(50, 0, 98.39, hit), world.surfaceAt(50, 0, 98.41, hit));
    for (const [x, z, yRef, maxDist, maxDy] of [[50, 3, 100, 5, 4.5], [50, 9, 100, 5, 4.5], [50, 9, 100, 7, 4.5], [50, 3, 106, 5, 4.5], [50, 3, 106, 5, 8], [150, 148, 100, 60, 4.5], [330, 20, 100, 25, 4.5]]) {
      const located = world.locate(x, z, yRef, maxDist, hit, maxDy);
      out('locate', located, located ? hit.edge!.id : -1, located ? hit.s : 0, located ? hit.off : 0, located ? hit.y : 0, located ? hit.zone : 0);
    }
    for (const [x, z, maxDist, skip] of [[50, 30, 40, 0], [50, 30, 20, 0], [150, 148, 60, 0], [150, 148, 60, 256], [-30, -30, 50, 0], [100.5, 100.2, 5, 0]]) {
      const near = world.nearestRoad(x, z, maxDist, skip, hit);
      out('nearest', near, near ? hit.edge!.id : -1, near ? hit.s : 0, near ? hit.off : 0, near ? hit.y : 0, near ? hit.zone : 0);
    }
    const ids = (visit: (cb: (v: { id: number }, seg?: number) => void) => void): string => {
      const list: string[] = [];
      visit((v, seg) => list.push(seg === undefined ? String(v.id) : `${v.id}:${seg}`));
      return list.join(',') || '-';
    };
    out('edgesInBox', ids((cb) => world.edgesInBox(90, 90, 160, 110, cb)));
    out('nodesInBox', ids((cb) => world.nodesInBox(90, -10, 310, 110, cb)));
    out('segmentsInBox', ids((cb) => world.segmentsInBox(95, 95, 105, 105, cb)));
    const e = world.edges[town.street(0, 1, 1, 1)];
    out('banned', world.isBanned(e, world.nodes[town.node(1, 1)], world.edges[town.street(1, 1, 1, 0)]), world.isBanned(e, world.nodes[town.node(1, 1)], world.edges[town.street(1, 1, 2, 1)]), world.isBanned(world.edges[town.street(1, 1, 1, 0)], world.nodes[town.node(1, 1)], e));
    out('planeY', world.planeY(world.nodes[5], 110, 95), world.edgeY(e, 3, 3, 100), world.edgeY(e, 50, 50, 100), world.edgeY(e, 97, 97, 101));
    // packed points and back; the cuts of a junction worked out again; its shape
    const p = [12.34, 100.5, -7.01, 13.0, 100.56, -9.99, -250.25, 99.0, 1234.56];
    out('packed', ...packEdgePoints(p), ...edgePoints(packEdgePoints(p)));
    const n5 = world.nodes[5];
    const before = n5.arms.map((a: { cut: number }) => a.cut);
    computeJunctionCuts(n5);
    out('cuts', ...before, ...n5.arms.map((a: { cut: number }) => a.cut), ...n5.fr);
    const sh = buildJunctionShape(n5);
    out('shape', sh ? sh.ring.length : -1, sh ? sumOf(sh.ring) : 0);
  }
}

if (part === 'lanes') {
  const town = makeTown(townOptions(false));
  const world = town.world;
  const lanes = town.lanes;
  out('graph', lanes.lanes.length, lanes.connectors.length, lanes.edgeLanes.length);
  const tmp = newPolySample();
  for (const l of lanes.lanes) {
    lanePoint(l, l.len * 0.3, tmp);
    out('lane', l.id, l.edge.id, l.dir, l.idx, l.count, l.off, l.width, l.s0, l.s1, l.len, l.from.id, l.to.id, l.armTo.edge.id, l.armFrom.edge.id, l.limit, l.stopAt, l.out.length, l.in.length, tmp.x, tmp.y, tmp.z, tmp.tx, tmp.tz);
  }
  for (const c of lanes.connectors) {
    out('path', c.id, c.node.id, c.from.id, c.to.id, c.turn, c.len, c.pl.cum.length, lineSum(c.pl.pts));
  }
  for (const c of lanes.connectors) {
    for (const k of c.conflicts) out('conflict', c.id, k.other.id, k.sSelf, k.sOther, k.merge, k.yield);
    for (const s of c.siblings) out('sibling', c.id, s.other.id, s.until, s.untilOther);
  }
  for (const n of world.nodes) out('through', n.id, ...lanes.pathsThrough(n).map((c: { id: number }) => c.id));
  for (const n of world.nodes) for (const a of n.arms) out('into', n.id, a.idx, lanes.lanesInto(a).map((l: { id: number }) => l.id).join(',') || '-', lanes.lanesOutOf(a).map((l: { id: number }) => l.id).join(',') || '-');
  // ways through the lanes
  const lane = (i0: number, j0: number, i1: number, j1: number, back = false): { id: number } => {
    const el = lanes.edgeLanes[town.street(i0, j0, i1, j1)];
    return back ? el.back[0] : el.fwd[0];
  };
  const routes: Array<[unknown, number, boolean, number]> = [
    [lane(0, 0, 1, 0), town.street(2, 2, 3, 2), false, 0],
    [lane(0, 0, 1, 0), town.street(2, 2, 3, 2), true, 0],
    [lane(0, 1, 1, 1), town.street(1, 0, 1, 1), false, 0],
    [lane(0, 1, 1, 1), town.street(1, 0, 1, 1), false, 1],
    [lane(0, 1, 1, 1), town.street(1, 0, 1, 1), false, -1],
    [lane(2, 1, 3, 1, true), town.street(0, 0, 1, 0), false, 0],
    [lane(3, 0, 3, 1), town.street(3, 0, 3, 1), false, 0],
    [lane(0, 0, 0, 1), world.edges.length - 1, false, 0],
  ];
  for (const [start, goal, forTraffic, goalDir] of routes) {
    const r = lanes.route(start, world.edges[goal], forTraffic, goalDir);
    out('route', (start as { id: number }).id, goal, forTraffic, goalDir, r ? r.length : -1, ...(r ?? []).map((c: { id: number }) => c.id));
  }
  lanes.longVehicle = true;
  lanes.wayCost = (way: number): number => (way >> 1 === town.street(1, 0, 2, 0) ? Infinity : way >> 1 === town.street(1, 1, 2, 1) ? 500 : 0);
  for (const [start, goal, forTraffic, goalDir] of routes.slice(0, 3)) {
    const r = lanes.route(start, world.edges[goal], forTraffic, goalDir);
    out('routeCost', r ? r.length : -1, ...(r ?? []).map((c: { id: number }) => c.id));
  }
  lanes.longVehicle = false;
  lanes.wayCost = null;
  // what is near a point: the middle of a lane, a path through a junction; walls; posts
  const foot = { x: 0, z: 0, d: 0 };
  for (const [x, z, y, reach] of [[50, 1, 100, 3], [50, 1, 105, 3], [50, 1, NaN, 3], [50, 6, 100, 3], [100, 100, 100, 4], [103, 97.5, 100, 2], [150, 101.6, 100, 2], [360, 0.4, 100, 3], [50, 50, 100, 10]]) {
    const d = lanes.nearestPath(x, z, y, reach, foot);
    out('nearpath', d, Number.isFinite(d) ? foot.x : 0, Number.isFinite(d) ? foot.z : 0);
  }
  out('walls', lanes.wallInLane(40, -6, 60, -6, 100, 0.8), lanes.wallInLane(40, -6, 60, -1.5, 100, 0.8), lanes.wallInLane(40, -6, 60, -1.5, 110, 0.8), lanes.wallInLane(40, -6, 60, -1.5, NaN, 0.8), lanes.wallInLane(96, 96, 104, 104, 100, 0.5), lanes.wallInLane(120, 120, 130, 130, 100, 2));
  for (const [x, z, need] of [[50, 1.3, 1.4], [50, 0, 1.4], [50, 7, 1.4], [100, 100, 1.4], [150, 100, 6]]) {
    const post = lanes.besideLanes(x, z, 100, need, foot);
    out('beside', post !== null, post ? foot.x : 0, post ? foot.z : 0, post ? foot.d : 0);
  }
  out('drivable', LaneGraph.drivable(world.edges[0], true), LaneGraph.drivable(world.edges[0], false));
  // the same network as the route planner sees it
  const g = routeGraphOf(lanes);
  out('rg', g.n, g.to.length, sumOf(g.len), sumOf(g.secs), sumOf(g.kind), sumOf(g.cls), sumOf(g.x0), sumOf(g.z0), sumOf(g.x1), sumOf(g.z1), sumOf(g.first), sumOf(g.to), sumOf(g.turn), sumOf(g.across), sumOf(g.firstIn), sumOf(g.from), sumOf(g.inTurn));
  out('main', sumOf(mainWays(g)), ...Array.from(mainWays(g)));
  const way = (i0: number, j0: number, i1: number, j1: number, back = false): number => wayOf(lane(i0, j0, i1, j1, back));
  const ways: Array<[number, number]> = [
    [way(0, 0, 1, 0), way(2, 2, 3, 2)],
    [way(0, 1, 1, 1), way(1, 0, 1, 1)],
    [way(2, 1, 3, 1, true), way(0, 0, 1, 0)],
    [way(0, 0, 0, 1), way(0, 0, 0, 1)],
    [way(0, 0, 0, 1), way(0, 0, 1, 0) + 1],
    [way(3, 0, 3, 1), (world.edges.length - 1) * 2],
  ];
  for (const [a, b] of ways) {
    const r = quickest(g, a, b);
    out('quickest', a, b, r ? r.length : -1, ...(r ?? []));
    const e = quickestToEdge(g, a, b >> 1);
    out('toEdge', e ? e.length : -1, ...(e ?? []));
  }
  routeFor.wayCost = (w: number): number => (w >> 1 === town.street(1, 0, 2, 0) ? Infinity : 0);
  for (const [a, b] of ways.slice(0, 2)) {
    const r = quickest(g, a, b);
    out('quickestCost', r ? r.length : -1, ...(r ?? []));
  }
  routeFor.wayCost = null;
}

if (part === 'xsection') {
  // the towns of tests/world/xsection.test.ts, as the map build makes them of OpenStreetMap's tags
  const { OsmData } = await import(`${root}/tools/mapgen/osm.ts`);
  const { buildRoadGraph, roadAttrs } = await import(`${root}/tools/mapgen/roads.ts`);
  process.env.DETAIL_REGION ??= 'all';
  type Tags = Record<string, string>;
  const town = (ways: Array<{ id: number; nodes: Array<[number, number, number]>; tags: Tags }>): object => {
    const osm = new OsmData();
    for (const w of ways) {
      for (const [id, x, z] of w.nodes) osm.nodes.set(id, { id, x, z, tags: null });
      const way = { id: w.id, refs: w.nodes.map((n) => n[0]), tags: w.tags };
      osm.ways.push(way);
      osm.wayById.set(way.id, way);
    }
    const g = buildRoadGraph(osm, { inWorld: () => true, builtUp: () => true });
    const json = {
      version: 2,
      meta: { name: 't', generated: '', minX: -500, maxX: 500, minZ: -500, maxZ: 500, tileSize: 500, originE: 0, originN: 0, attribution: [] },
      nodes: g.nodes,
      edges: g.edges,
      banned: [],
      signals: [],
      crossings: [],
      places: [],
      spawns: [],
      tiles: [],
    };
    computeAllCuts(json);
    const edgeOf: Record<string, number> = {};
    // (a way that is no road – a footway – has no edge)
    for (const w of ways) edgeOf[w.id] = g.wayEdges.get(w.id)?.[0] ?? -1;
    return { json, edgeOf };
  };
  const MAIN: Tags = { highway: 'residential', name: 'Hauptstraße', sidewalk: 'both', 'parking:right': 'lane', 'parking:right:markings': 'yes', 'parking:left': 'street_side', 'parking:left:orientation': 'diagonal', 'cycleway:right': 'lane' };
  const widths = roadAttrs({ highway: 'residential', 'parking:right': 'lane', 'parking:left': 'street_side', 'parking:left:orientation': 'perpendicular', 'cycleway:right': 'lane', sidewalk: 'both', 'sidewalk:left:width': '2.5', 'verge:left': 'grass' }, true);
  const fixture = {
    about: 'The towns of tests/world/xsection.test.ts of the original (v1.7.7) as its map build makes them: written by tools/diff/world_town.ts --part xsection',
    widths: { w: widths.w, lw: widths.lw, lf: widths.lf, lb: widths.lb, swL: widths.swL, swR: widths.swR, xl: widths.xl, xr: widths.xr },
    main: town([
      { id: 1, nodes: [[1, 0, 0], [2, 40, 0], [3, 100, 0]], tags: MAIN },
      { id: 2, nodes: [[3, 100, 0], [4, 220, 0]], tags: MAIN },
      { id: 3, nodes: [[3, 100, 0], [5, 100, 80]], tags: { highway: 'residential' } },
      { id: 4, nodes: [[3, 100, 0], [6, 100, -80]], tags: { highway: 'residential' } },
      { id: 5, nodes: [[2, 40, 0], [7, 40, 25]], tags: { highway: 'service', service: 'driveway' } },
    ]),
    against: town([{ id: 1, nodes: [[1, 0, 0], [2, 120, 0]], tags: { highway: 'residential', oneway: '-1', 'parking:left': 'lane' } }]),
    separate: town([
      { id: 1, nodes: [[1, 0, 0], [2, 120, 0]], tags: { highway: 'residential', 'sidewalk:left': 'separate', 'sidewalk:right': 'no' } },
      { id: 2, nodes: [[11, 0, -6], [12, 120, -6]], tags: { highway: 'footway', footway: 'sidewalk', width: '2' } },
    ]),
    median: town([
      { id: 1, nodes: [[1, 0, 9], [2, 200, 9]], tags: { highway: 'secondary', oneway: 'yes', 'median:contents': 'grass;trees;parking' } },
      { id: 2, nodes: [[3, 200, -9], [4, 0, -9]], tags: { highway: 'secondary', oneway: 'yes' } },
    ]),
  };
  lines.push(JSON.stringify(fixture));
}

if (part === 'values') {
  // the cross-section of a road, what calms the traffic on it, what narrows it: the helpers asked one by one
  const xs = await import(src('world/xsection.ts'));
  const rf = await import(src('world/roadfeatures.ts'));
  const nw = await import(src('world/narrowing.ts'));
  const sides: Array<Record<string, number> | undefined> = [undefined, {}, { p: 1 }, { p: 2, po: 1 }, { p: 2, po: 2, pw: 4.5 }, { p: 3 }, { p: 4, po: 1, v: 1.5 }, { p: 5, c: 1 }, { c: 1, cw: 1.85 }, { c: 2 }, { c: 2, cw: 2, v: 0.8, k: 1 }, { k: 2, g: 0.3 }, { k: 7 }, { p: 0, pw: 3 }, { el: 0 }, { el: 1, g: 0 }, { p: 2, g: 0.5 }];
  const pair = (v: [number, number] | null): unknown[] => v ?? ['-', '-'];
  for (const s of sides) {
    out('side', xs.parkWidth(s), xs.sideInner(s), xs.sideOuter(s), xs.kerbHeight(s), xs.sideIsPlain(s), xs.gutterWidth(s, true, 0.12), xs.gutterWidth(s, false, 0.12), xs.gutterWidth(s, true, 0.03), xs.gutterWidth(s, true, 0), xs.edgeLineOn(s, true), xs.edgeLineOn(s, false));
    for (const [half, beyond] of [[3.5, 2.5], [6, 0], [4, 6.5]]) {
      const l = xs.sideLayout(half, beyond, s);
      out('layout', l.kerb, l.kerbH, l.laneEdge, ...pair(l.cycleLane), ...pair(l.park), ...pair(l.verge), ...pair(l.track), ...pair(l.walk), l.outer);
    }
  }
  out('depth', xs.parkDepth(0), xs.parkDepth(1), xs.parkDepth(2), xs.parkDepth(9));
  out('median', xs.medianBits(undefined), xs.medianBits(''), xs.medianBits('grass'), xs.medianBits('grass;trees;hedge'), xs.medianBits(' Grass ; TREE;parking;paved;sett;nonsense'), xs.medianBits('cobblestone;paving_stones'));
  out('calmKind', ...['table', 'raised_table', 'hump', 'bump', 'cushion', 'choker', ''].map((v) => rf.calmKind(v)));
  const road = { calm: [20, 1, 4, 0.08, 45, 2, 3.7, 0.08, 3, 3, 0.6, 0.06, 97, 4, 2.5, 0.06, 60, 1, 4, 0.1], len: 100, cutA: 5, cutB: 6 };
  for (let i = 0; i < road.calm.length; i += 4) out('centre', rf.calmCentre(road, i), rf.calmHalfLength(road.calm[i + 1], road.calm[i + 2]));
  const lifts: number[] = [];
  for (let s = 0; s <= 100; s += 0.37) lifts.push(rf.calmLift(road, s));
  out('lift', ...lifts);
  out('breaks', ...rf.calmBreaks(road));
  const tight = { calm: [5, 1, 4, 0.08], len: 8, cutA: 3, cutB: 3 };
  out('noroom', rf.calmCentre(tight, 0), rf.calmLift(tight, 4), rf.calmLift({ calm: null, len: 8, cutA: 0, cutB: 0 }, 4), rf.calmBreaks({ calm: null, len: 1, cutA: 0, cutB: 0 }).length, rf.calmBreaks(tight).length);
  const arms = (mouths: number[]): unknown[] => {
    const node = { arms: [] as unknown[] };
    for (const m of mouths) node.arms.push({ edge: { mouth: m }, node });
    return node.arms;
  };
  out('mouth', ...[[1, 0, 0], [1, 1, 0], [1, 0, 0, 1], [0, 0], [1]].flatMap((m) => arms(m).map((a) => rf.flushMouth(a))));
  const feats = [
    { kind: 'bump', s: 30, tags: { traffic_calming: 'choker' } },
    { kind: 'bump', s: 50, tags: { traffic_calming: 'chicane', length: '6' } },
    { kind: 'bump', s: 70.123, tags: { traffic_calming: 'choker', side: 'left', 'traffic_calming:depth': '1.234', beacon: 'yes' } },
    { kind: 'bump', s: 80, tags: { traffic_calming: 'choker', 'traffic_calming:side': 'right', planted: 'no', tree: 'no', length: '50' } },
    { kind: 'bump', s: 90, tags: { traffic_calming: 'hump' } },
    { kind: 'crossing', s: 95, tags: { traffic_calming: 'choker' } },
    { kind: 'bump', s: 10, tags: { traffic_calming: 'chicane', length: 'abc', 'traffic_calming:beacon': 'yes', side: '' } },
  ];
  const bo = nw.buildOutEntries(feats);
  out('entries', ...bo);
  const roads = [
    { width: 5.5, marginL: 0, marginR: 0, laneW: 2.75, lanesF: 1, lanesB: 1, flags: 0, len: 100, cutA: 4, cutB: 4 },
    { width: 7, marginL: 0.25, marginR: 0.5, laneW: 3.25, lanesF: 1, lanesB: 1, flags: 64, len: 100, cutA: 0, cutB: 0 },
    { width: 3.5, marginL: 0, marginR: 0, laneW: 3.5, lanesF: 1, lanesB: 0, flags: 0, len: 100, cutA: 0, cutB: 0 },
    { width: 13, marginL: 0, marginR: 0, laneW: 3.25, lanesF: 2, lanesB: 2, flags: 0, len: 60, cutA: 20, cutB: 20 },
  ];
  for (const e of roads) {
    out('room', nw.buildOutRoom(e, 1, false), nw.buildOutRoom(e, -1, false), nw.buildOutRoom(e, 1, true), nw.buildOutRoom(e, -1, true));
    for (const n of nw.narrowingsOf(e, bo)) out('narrowing', n.s0, n.s1, n.at, n.side, n.depth, n.lo, n.hi, n.single, n.flags);
  }
  out('none', nw.narrowingsOf(roads[0], null).length, nw.narrowingsOf(roads[0], undefined).length, nw.narrowingsOf(roads[0], []).length);
}

if (part === 'net') {
  // the road network of a whole map as one small file: made of the town, written, read, searched by place, changed by a diff
  const { packNet, unpackNet, routeGraphOfNet, netPolyline, POLY_UNIT } = await import(src('world/netfile.ts'));
  const { NetIndex } = await import(src('world/netindex.ts'));
  const nd = await import(src('world/netdiff.ts'));
  const town = makeTown(townOptions(false));
  const world = town.world;
  const g = routeGraphOf(town.lanes);
  const E = world.edges.length;
  const sum = (b: ArrayLike<number>): number => {
    let t = 0;
    for (let i = 0; i < b.length; i++) t += b[i] * ((i % 251) + 1);
    return t;
  };
  const waySpeed = new Uint8Array(E * 2);
  const polyFirst = new Uint32Array(E + 1);
  const poly: number[] = [];
  for (const e of world.edges) {
    const el = town.lanes.edgeLanes[e.id];
    waySpeed[e.id * 2] = el.fwd.length ? Math.min(255, Math.round(el.fwd[0].limit * 3.6)) : 0;
    waySpeed[e.id * 2 + 1] = el.back.length ? Math.min(255, Math.round(el.back[0].limit * 3.6)) : 0;
    polyFirst[e.id] = poly.length / 2;
    // the points between its ends, each as the step from the one before in quarter metres
    let x = Math.fround(e.a.x);
    let z = Math.fround(e.a.z);
    const p = e.pl.pts;
    for (let i = 1; i < e.pl.cum.length - 1; i++) {
      const sx = Math.round((p[i * 3] - x) / POLY_UNIT);
      const sz = Math.round((p[i * 3 + 2] - z) / POLY_UNIT);
      poly.push(sx, sz);
      x += sx * POLY_UNIT;
      z += sz * POLY_UNIT;
    }
  }
  polyFirst[E] = poly.length / 2;
  const net = {
    nodeX: Float32Array.from(world.nodes, (n: { x: number }) => n.x + 0.1),
    nodeZ: Float32Array.from(world.nodes, (n: { z: number }) => n.z - 0.3),
    edgeA: Uint32Array.from(world.edges, (e: { a: { id: number } }) => e.a.id),
    edgeB: Uint32Array.from(world.edges, (e: { b: { id: number } }) => e.b.id),
    edgeLen: Float32Array.from(world.edges, (e: { len: number }) => e.len + 0.07),
    edgeCls: Uint8Array.from(world.edges, (e: { id: number; cls: number }) => (e.id % 5 === 0 ? 6 : e.cls)),
    wayKind: g.kind,
    waySpeed,
    first: g.first,
    to: g.to,
    turn: g.turn,
    across: Uint16Array.from(g.across, (v: number) => Math.round(v * 10)),
    polyFirst,
    poly: Int16Array.from(poly),
  };
  const bytes = packNet(net);
  out('packed', bytes.length, sum(bytes), new TextDecoder().decode(bytes.subarray(8, 8 + new DataView(bytes.buffer).getUint32(4, true))).trim());
  const back = unpackNet(bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength));
  out('back', sumOf(back.nodeX), sumOf(back.nodeZ), sumOf(back.edgeA), sumOf(back.edgeB), sumOf(back.edgeLen), sumOf(back.edgeCls), sumOf(back.wayKind), sumOf(back.waySpeed), sumOf(back.first), sumOf(back.to), sumOf(back.turn), sumOf(back.across), sumOf(back.polyFirst), sumOf(back.poly));
  const rg = routeGraphOfNet(back);
  out('rg', rg.n, sumOf(rg.len), sumOf(rg.secs), sumOf(rg.kind), sumOf(rg.cls), sumOf(rg.x0), sumOf(rg.z0), sumOf(rg.x1), sumOf(rg.z1), sumOf(rg.first), sumOf(rg.to), sumOf(rg.turn), sumOf(rg.across), sumOf(rg.firstIn), sumOf(rg.from), sumOf(rg.inTurn));
  const index = new NetIndex(back);
  out('main', ...Array.from(index.main));
  for (let e = 0; e < E; e += 4) out('course', e, ...netPolyline(back, e));
  const usable = (e: number): boolean => e % 2 === 1;
  for (const [x, z, maxDist] of [[50, 3, 10], [50, 3, 2], [150, 148, 80], [-30, -30, 50], [103, 97, 5], [330, 5, 40], [1000, 1000, 50], [99.9, 60, 200]]) {
    const a = index.nearest(x, z, maxDist);
    const b = index.nearest(x, z, maxDist, usable);
    out('nearest', a ? a.edge : -1, a ? a.x : 0, a ? a.z : 0, a ? a.yaw : 0, a ? a.d : 0, b ? b.edge : -1, b ? b.d : 0);
  }
  for (const [x0, z0, x1, z1] of [[90, 90, 160, 110], [-50, -50, 500, 500], [900, 900, 1100, 1100], [-2000, -10, 3000, 10], [150, 50, 150, 50]]) {
    const all: number[] = [];
    const big: number[] = [];
    index.edgesInBox(x0, z0, x1, z1, (e: number) => all.push(e));
    index.edgesInBox(x0, z0, x1, z1, (e: number) => big.push(e), true);
    out('box', all.join(',') || '-', big.join(',') || '-');
  }
  {
    const path = { text: '', moveTo(x: number, z: number): void { this.text += `M${x},${z}`; }, lineTo(x: number, z: number): void { this.text += `L${x},${z}`; } };
    index.trace(3, path);
    index.trace(E - 1, path);
    out('trace', path.text);
  }
  for (const [a, b] of [[0, 20], [6, 1], [9, 30], [2, 2]]) {
    const r = quickest(index.graph, a, b);
    out('quickest', a, b, r ? r.length : -1, ...(r ?? []));
  }
  // a correction of values as a diff
  out('fingerprint', nd.fingerprint(back));
  const neu = { ...back, wayKind: back.wayKind.slice(), across: back.across.slice(), turn: back.turn.slice(), edgeCls: back.edgeCls.slice(), waySpeed: back.waySpeed.slice() };
  neu.wayKind[3] ^= 64;
  neu.wayKind[20] = 255;
  neu.across[0] = 65535;
  neu.across[7] += 13;
  neu.turn[5] = (neu.turn[5] + 1) % 4;
  neu.waySpeed[11] = 30;
  const diff = nd.netDiff(back, neu);
  out('diff', diff.v, diff.kind, diff.edges, diff.hash, JSON.stringify(diff.set));
  const packedDiff = nd.packNetDiff(diff);
  out('difftext', new TextDecoder().decode(packedDiff));
  const read = nd.unpackNetDiff(packedDiff);
  const applied = nd.applyNetDiff(back, read);
  let same = true;
  for (const name of ['wayKind', 'waySpeed', 'turn', 'across', 'edgeCls', 'first', 'to', 'edgeA']) for (let i = 0; i < applied[name].length; i++) if (applied[name][i] !== neu[name][i]) same = false;
  out('applied', same, nd.matchesFingerprint(back, read), nd.matchesFingerprint(applied, read), sumOf(applied.wayKind), sumOf(applied.across), sumOf(applied.turn), sumOf(applied.waySpeed), sumOf(back.wayKind), sumOf(back.across));
  out('nodiff', JSON.stringify(nd.netDiff(back, back).set), new TextDecoder().decode(nd.packNetDiff(nd.netDiff(back, back))));
  const fails = (f: () => unknown): string => {
    try {
      f();
      return 'no error';
    } catch (err) {
      return (err as Error).message;
    }
  };
  out('wrongKind', fails(() => nd.applyNetDiff(back, { ...read, kind: 'other' })));
  out('wrongHash', fails(() => nd.applyNetDiff(back, { ...read, hash: 'abc' })));
  out('wrongEdges', fails(() => nd.applyNetDiff(back, { ...read, edges: 1 })));
  out('wrongSection', fails(() => nd.applyNetDiff(back, { ...read, set: { first: [[0, 1]] } })));
  out('outOfRange', fails(() => nd.applyNetDiff(back, { ...read, set: { turn: [[100000, 1]] } })), fails(() => nd.applyNetDiff(back, { ...read, set: { turn: [[-1, 1]] } })));
  out('wraps', nd.applyNetDiff(back, { ...read, set: { wayKind: [[0, 300], [1, -1], [2, 2.9]], across: [[1, 70000]] } }).wayKind.slice(0, 3).join(','), nd.applyNetDiff(back, { ...read, set: { across: [[1, 70000]] } }).across[1]);
  out('changedLength', fails(() => nd.netDiff(back, { ...back, turn: back.turn.slice(1) })));
  const enc = (t: string): Uint8Array => new TextEncoder().encode(t);
  out('unpack', nd.unpackNetDiff(enc('garbage')) === null, nd.unpackNetDiff(enc('{"v":2,"kind":"netdiff","hash":"x","set":{}}')) === null, nd.unpackNetDiff(enc('{"v":1,"kind":"net","hash":"x","set":{}}')) === null, nd.unpackNetDiff(enc('{"v":1,"kind":"netdiff","hash":7,"set":{}}')) === null, nd.unpackNetDiff(enc('{"v":1,"kind":"netdiff","hash":"x"}')) === null, nd.unpackNetDiff(enc('{"v":1,"kind":"netdiff","edges":5,"hash":"x","set":{"turn":[[1,2]]}}')) !== null, nd.unpackNetDiff(enc('null')) === null, nd.unpackNetDiff(enc('[]')) === null);
  // (a file cut off inside its first eight bytes: the original fails with a message of its engine's – that it fails is what counts)
  out('notNet', fails(() => unpackNet(nd.packNetDiff(diff).buffer)), fails(() => unpackNet(new TextEncoder().encode('FSB1').buffer)) !== 'no error');
}

void samplePolyline;
void World;
process.stdout.write(lines.join('\n') + '\n');
