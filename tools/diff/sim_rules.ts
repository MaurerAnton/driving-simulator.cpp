// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The original's rule monitor watching a car that does most things wrong – beside
 * tools/dev/sim_rules.cpp:
 *
 *   node tools/diff/sim_rules.ts <path of the original> <out: world.json> [town] > a.txt
 *   build/sim_rules <world.json> [town] > b.txt
 *   tools/diff/compare.py a.txt b.txt --atol 1e-9
 *
 * The car is moved along a tour of the town (as tools/diff/sim_drive.ts makes one), street by
 * street with another fault: far too fast, on the other side of the road, on the pavement, a
 * turn not indicated, one indicated to the wrong side, the handbrake on, no seat belt – and
 * past every light whatever it shows.  In a town with one-way streets (wide) it then drives
 * up one against its direction.  Three times a second a line of what the monitor knows; at the
 * end every fault it noted, with its values.
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
const at = (p: string): string => pathToFileURL(join(ROOT, p)).href;
const { makeTown } = await import(at('tests/game/town.ts'));
const { SignalSystem } = await import(at('src/sim/signals.ts'));
const { Traffic } = await import(at('src/sim/traffic.ts'));
const { RuleMonitor } = await import(at('src/sim/rules.ts'));
const { TURN, lanePoint } = await import(at('src/world/lanes.ts'));
const { newPolySample, samplePolyline } = await import(at('src/core/geom.ts'));

/* eslint-disable @typescript-eslint/no-explicit-any */
const opt = townOptions(WHICH);
const town = makeTown(opt);
writeFileSync(OUT, JSON.stringify(town.json));
const world = town.world;
const lanes = town.lanes;
const signals: any = new SignalSystem(world.signals);
const traffic: any = new Traffic(world, lanes, signals);
traffic.targetCount = 0;
traffic.crowd.target = 0;
traffic.cyclists.update = (): void => {};
const rules: any = new RuleMonitor(world, lanes, signals, traffic);
const smp = newPolySample();

// the way: turns in a fixed order, no lane twice (as tools/diff/sim_drive.ts)
const start: any = lanes.edgeLanes[town.street(0, 1, 1, 1)].fwd[0];
const PATTERN = [TURN.STRAIGHT, TURN.RIGHT, TURN.LEFT, TURN.STRAIGHT, TURN.LEFT, TURN.RIGHT, TURN.STRAIGHT, TURN.STRAIGHT, TURN.LEFT, TURN.RIGHT];
const route: any[] = [];
const taken = new Set<any>([start]);
for (let k = 0, l = start; k < 16; k++) {
  const want = PATTERN[k % PATTERN.length];
  const free = l.out.filter((q: any) => !taken.has(q.to));
  const c = free.find((q: any) => q.turn === want) ?? free.find((q: any) => q.turn !== TURN.UTURN) ?? free[0];
  if (!c) break;
  route.push(c);
  taken.add(c.to);
  l = c.to;
}

/** what is done wrong on the k-th street of the way: speed (m/s), metres left of the middle of the lane, the indicator, the handbrake, the belt */
const PLAN: Array<{ speed: number; off: number; indicator: number; handbrake: number; belt: boolean }> = [
  { speed: 21, off: 0, indicator: 1, handbrake: 0, belt: true },
  { speed: 8, off: 2.75, indicator: 1, handbrake: 0, belt: true },
  { speed: 8, off: -2.4, indicator: 1, handbrake: 0, belt: true },
  { speed: 8, off: 0, indicator: 0, handbrake: 0, belt: true },
  { speed: 8, off: 0, indicator: -1, handbrake: 0, belt: true },
  { speed: 8, off: 0, indicator: 1, handbrake: 1, belt: true },
  { speed: 12, off: 0, indicator: 1, handbrake: 0, belt: true },
  { speed: 8, off: 0, indicator: 1, handbrake: 0, belt: false },
];

const DT = 1 / 30;
const out: string[] = [`route ${route.map((c) => `${c.from.id}>${c.to.id}`).join(' ')}`];
let step = 0;
const see = (x: number, y: number, z: number, yaw: number, speed: number, indicator: number, look: number, handbrake: number, belt: boolean): void => {
  signals.update(DT);
  traffic.update(DT, { x, y: y + 40, z, yaw, speed: 0, length: 4.3, width: 1.8 });
  rules.update(DT, { x, y, z, yaw, speed, indicator, hazard: false, lookYaw: look, seatbelt: belt, handbrake, stalled: false, hitScenery: 0, length: 4.3 }, traffic.events);
  step++;
  if (step % 10 !== 0) return;
  const c = rules.context;
  const j = c.junction;
  out.push(
    `step ${step} ${x} ${z} ${speed} ${c.limit} ${c.zone} ${c.urban ? 1 : 0} ${c.onRoad ? 1 : 0} ${c.wrongWay ? 1 : 0} ${j ? j.dist : -1} ${j ? j.role : -9} ${j ? j.ctrl : -9} ${j ? j.signal : -9} ${j ? j.arrow : -9} ${j && j.dark ? 1 : 0} ` +
      `${rules.laneNow ? rules.laneNow.id : -1} ${rules.laneFront} ${rules.log.length} ${rules.points} ${(c.roadName || '-').replace('Street ', 'Street_')}`,
  );
};
// street by street: its lane, then the path through the junction at its end
for (let k = 0, lane = start; k <= route.length; k++) {
  const plan = PLAN[k % PLAN.length];
  const conn = route[k] ?? null;
  // the turn at the end of this street: indicated as the plan says (1: as it should be, 0: not, −1: to the wrong side)
  const turn = conn ? (conn.turn === TURN.LEFT ? -1 : conn.turn === TURN.RIGHT ? 1 : 0) : 0;
  const shown = turn * plan.indicator;
  for (let d = 2; d < lane.len; d += plan.speed * DT) {
    lanePoint(lane, d, smp);
    const off = d > 15 && d < lane.len - 25 ? plan.off : 0;
    const near = lane.len - d < 40;
    see(smp.x + smp.tz * off, smp.y, smp.z - smp.tx * off, Math.atan2(-smp.tx, -smp.tz), plan.speed, near ? shown : 0, near && lane.len - d > 8 && lane.len - d < 20 ? (shown < 0 ? 1.3 : shown > 0 ? -1.3 : 0) : 0, plan.handbrake, plan.belt);
  }
  if (!conn) break;
  for (let d = 0; d < conn.len; d += plan.speed * DT) {
    samplePolyline(conn.pl, d, smp);
    see(smp.x, smp.y, smp.z, Math.atan2(-smp.tx, -smp.tz), plan.speed, shown, 0, plan.handbrake, plan.belt);
  }
  lane = conn.to;
}
// a one-way street, driven up against its direction: westwards along the street from (0, 2) to (1, 2)
if (opt.oneWay) {
  for (let x = opt.block - 10; x > 10; x -= 8 * DT) see(x, 100, 2 * opt.block, Math.PI / 2, 8, 0, 0, 0, true);
}
for (const f of rules.log) out.push(`fault ${f.id} ${f.severity} ${f.time} ${f.x} ${f.z} ${Object.entries(f.data).map(([k, v]) => `${k}=${v}`).join(' ')}`.trimEnd());
out.push(`end faults ${rules.log.length} points ${rules.points} failed ${rules.failed ? 1 : 0}`);
process.stdout.write(out.join('\n') + '\n');
