// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The original's traffic on a test town, second by second – beside tools/dev/traffic_sim.cpp,
 * the port of the original's headless traffic check (tests/traffic.sim.ts) on a small map:
 *
 *   node tools/diff/sim_traffic.ts <path of the original> <out: world.json> [town] [seconds] [player] > a.txt
 *   build/traffic_sim <world.json> [town] [seconds] [player] > b.txt
 *   tools/diff/compare.py a.txt b.txt
 *
 * `town`: plain | lights | wide (tools/diff/sim_towns.ts); `player`: anchor (parked 40 m above the
 * middle of the town, as in tests/traffic.sim.ts: nobody brakes for it) or ghost (a car that
 * drives east along the second row again and again, through red lights and whoever is in its
 * way: the cars brake for it, are cut up by it, and are run into) or parked (the anchor, and two
 * vehicles parked for good in a lane: the traffic goes round them) or buses (the anchor, and a bus
 * stop on every street: half of what appears is a city bus that calls at them).
 *
 * The traffic draws its numbers from its own generator (a fixed seed), so both runs see the
 * same.  The people on foot and the cyclists the original's traffic makes for itself are not
 * in the port yet: here none walk (crowd.target = 0) and none ride (the cyclists' step is
 * left out) – the script says so if any turn up all the same.
 */
import { writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { pathToFileURL } from 'node:url';
import { strictMath } from './sim_math.ts';
import { townOptions } from './sim_towns.ts';

strictMath();

const ROOT = process.argv[2] ?? process.cwd();
const OUT = process.argv[3] ?? 'world.json';
const WHICH = process.argv[4] ?? 'lights';
const SECONDS = Number(process.argv[5] ?? 300);
const PLAYER = process.argv[6] ?? 'anchor';
const at = (p: string): string => pathToFileURL(join(ROOT, p)).href;
const { makeTown } = await import(at('tests/game/town.ts'));
const { SignalSystem } = await import(at('src/sim/signals.ts'));
const { Traffic, boxesOverlap } = await import(at('src/sim/traffic.ts'));

/* eslint-disable @typescript-eslint/no-explicit-any */
const opt = townOptions(WHICH);
const town = makeTown(opt);
writeFileSync(OUT, JSON.stringify(town.json));
const signals: any = new SignalSystem(town.world.signals);
const traffic: any = new Traffic(town.world, town.lanes, signals);
traffic.targetCount = 20;
traffic.crowd.target = 0;
traffic.cyclists.update = (): void => {};

if (PLAYER === 'buses') {
  // a stop half-way along every street long enough for one: every other vehicle that appears is a city bus
  traffic.busStops = (lane: any) => (lane.len > 60 ? [{ s: lane.len * 0.5, id: lane.id * 64 }] : []);
  traffic.busShare = 0.5;
}
if (PLAYER === 'parked') {
  // two vehicles parked for good in a lane: eastbound on the second row, southbound on the third column
  traffic.obstacles = [
    { x: 1.4 * opt.block, y: 100, z: opt.block + 1.375, yaw: -Math.PI / 2, speed: 0, length: 4.4, width: 1.8, parked: true },
    { x: 2 * opt.block - 1.375, y: 100, z: 1.5 * opt.block, yaw: Math.PI, speed: 0, length: 4.4, width: 1.8, parked: true },
  ];
}

const cx = ((opt.cols - 1) * opt.block) / 2;
const cz = ((opt.rows - 1) * opt.block) / 2;
const dt = 1 / 30;
const out: string[] = [];
let overlaps = 0;
let maxWait = 0;
let removedStuck = 0;
let speedSum = 0;
let samples = 0;
let nan = 0;
let collisions = 0;
let forced = 0;
let contacts = 0;
const seenOverlap = new Set<string>();
const steps = Math.round(SECONDS * 30);
for (let step = 0; step < steps; step++) {
  const t = step * dt;
  signals.update(dt);
  const before = traffic.cars.length;
  // anchor: parked 40 m above the ground so no car ever brakes for it; ghost: eastwards along the second row, again and again
  const player =
    PLAYER === 'ghost'
      ? { x: 30 + ((8 * t) % ((opt.cols - 1) * opt.block - 60)), y: 100, z: opt.block + 1.375, yaw: -Math.PI / 2, speed: 8, length: 4.3, width: 1.8 }
      : { x: cx, y: 100 + 40, z: cz, yaw: 0, speed: 0, length: 4.3, width: 1.8 };
  traffic.update(dt, player);
  if (traffic.cars.length < before) removedStuck += traffic.cars.filter((c: any) => c.stuck > 44).length;
  for (const ev of traffic.events) {
    if (ev.type === 'collision') collisions++;
    else if (ev.type === 'forcedBrake') forced++;
    else if (ev.type === 'contact') contacts++;
  }
  for (const c of traffic.cars) {
    if (!Number.isFinite(c.x + c.z + c.v)) nan++;
    speedSum += c.v;
    samples++;
    maxWait = Math.max(maxWait, c.stuck);
  }
  if (step % 6 === 0) {
    const cars = traffic.cars;
    for (let i = 0; i < cars.length; i++) {
      for (let j = i + 1; j < cars.length; j++) {
        const a = cars[i];
        const b = cars[j];
        if (Math.abs(a.x - b.x) > 6 || Math.abs(a.z - b.z) > 6 || Math.abs(a.y - b.y) > 2.5) continue;
        if (boxesOverlap(a.x, a.z, a.yaw, a.length / 2 - 0.25, a.width / 2 - 0.2, b.x, b.z, b.yaw, b.length / 2 - 0.25, b.width / 2 - 0.2)) {
          const key = `${a.id}:${b.id}`;
          if (!seenOverlap.has(key)) {
            seenOverlap.add(key);
            overlaps++;
          }
        }
      }
    }
  }
  if ((step + 1) % 30 !== 0) continue;
  out.push(`t ${(step + 1) / 30} cars ${traffic.cars.length} collisions ${collisions} forced ${forced} contacts ${contacts} overlaps ${overlaps} signal ${signals.controllers.map((_: any, i: number) => signals.get(i, 0)).join(' ')}`);
  for (const c of traffic.cars) {
    const d = c.drawn ?? { x: c.x, z: c.z, yaw: c.yaw };
    out.push(
      `car ${c.id} ${c.lane ? c.lane.id : -1} ${c.conn ? c.conn.from.id : -1} ${c.conn ? c.conn.to.id : -1} ${c.s} ${c.v} ${c.a} ${c.x} ${c.y} ${c.z} ${c.yaw} ${c.pitch} ${c.blinker} ${c.braking ? 1 : 0} ${c.stuck} ${c.crashed} ${c.waited} ${c.yieldWait} ${d.x} ${d.z} ${d.yaw} ${d.steer ?? 0} ${c.past ? c.past.phase : '-'} ${c.past ? c.past.off : 0} ${c.color} ${c.style} ${c.temper}`,
    );
  }
}
out.push(
  `end cars ${traffic.cars.length} meanSpeedKmh ${(speedSum / Math.max(samples, 1)) * 3.6} longestStandstill ${maxWait} removedStuck ${removedStuck} overlaps ${overlaps} nan ${nan} collisions ${collisions} forced ${forced} contacts ${contacts}`,
);
if (traffic.peds.length > 0 || traffic.cyclists.list.length > 0) throw new Error('people or cyclists turned up: the two runs are not alike');
process.stdout.write(out.join('\n') + '\n');
