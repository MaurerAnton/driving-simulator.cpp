// SPDX-License-Identifier: AGPL-3.0-only
/**
 * What the original makes of a small test town (tests/game/town.ts), printed
 * as numbers: the lines tests/world_test.cpp and tests/lanes_test.cpp expect
 * from the port (they hold this output and print the same themselves).
 *
 *   cd /home/user/driving-simulator
 *   node /path/to/port/tools/diff/world_town.ts --part world   > …   (the text in tests/world_test.cpp)
 *   node /path/to/port/tools/diff/world_town.ts --part lanes   > …   (the text in tests/lanes_test.cpp)
 *   node /path/to/port/tools/diff/world_town.ts --part xsection > tests/fixtures/xsection-towns.json
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

void samplePolyline;
void World;
process.stdout.write(lines.join('\n') + '\n');
