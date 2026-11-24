// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The original's driver of the autopilot at the wheel of the real vehicle model, among the
 * traffic and under the eyes of the rule monitor, on a test town – beside tools/dev/sim_drive.cpp:
 *
 *   node tools/diff/sim_drive.ts <path of the original> <out: world.json> [town] [seconds] [cars] [variant] [vehicle] > a.txt
 *   build/sim_drive <world.json> [town] [seconds] [cars] [variant] [vehicle] > b.txt
 *   tools/diff/compare.py a.txt b.txt
 *
 * The loop is the one of the original's virtual learner driver (tests/drive.sim.ts): the driver
 * decides, the vehicle is stepped, then the lights, the traffic and the rule monitor.  The way
 * is a tour of the town made of turns in a fixed order (straight on, right, left, …) that takes
 * no lane twice and ends on a street it has not been on; there the car is told to halt.  Ten times a second a line is printed:
 * where the car is, what the driver does and says, what the monitor knows; at the end the
 * faults it noted.  `variant`: tour | deadend (up the last column into the cul-de-sac of the town
 * `wide`: turned round there) | leftlane (it starts beside the lane its way begins on) | reckless (lights and
 * stop signs are ignored: what the monitor and the traffic make of that); `vehicle`:
 * car | truck.  (No people on foot and no cyclists, as in tools/diff/sim_traffic.ts.)
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
const CARS = Number(process.argv[6] ?? 12);
const VARIANT = process.argv[7] ?? 'tour';
const VEHICLE = process.argv[8] ?? 'car';
const at = (p: string): string => pathToFileURL(join(ROOT, p)).href;
const { makeTown } = await import(at('tests/game/town.ts'));
const { SignalSystem } = await import(at('src/sim/signals.ts'));
const { Traffic } = await import(at('src/sim/traffic.ts'));
const { RuleMonitor } = await import(at('src/sim/rules.ts'));
const { AutoDriver } = await import(at('src/sim/autodriver.ts'));
const { WorldGround } = await import(at('src/sim/ground.ts'));
const { COMPACT_CAR_AUTOMATIC, TRUCK, Vehicle } = await import(at('src/sim/vehicle/index.ts'));
const { TURN, lanePoint } = await import(at('src/world/lanes.ts'));
const { newPolySample } = await import(at('src/core/geom.ts'));
const { newSurfaceHit } = await import(at('src/world/world.ts'));

/* eslint-disable @typescript-eslint/no-explicit-any */
const opt = townOptions(WHICH);
const town = makeTown(opt);
// the vehicle: the compact car with the automatic gearbox, or the truck (measured from the middle of its wheelbase: its front 3.75 m ahead)
const SPEC = VEHICLE === 'truck' ? TRUCK : COMPACT_CAR_AUTOMATIC;
const FRONT = VEHICLE === 'truck' ? 3.75 : 2.14;
const LENGTH = SPEC.length ?? 4.28;
const WIDTH = SPEC.width ?? 1.79;
writeFileSync(OUT, JSON.stringify(town.json));
const world = town.world;
const lanes = town.lanes;
const signals: any = new SignalSystem(world.signals);
const traffic: any = new Traffic(world, lanes, signals);
traffic.targetCount = CARS;
traffic.crowd.target = 0;
traffic.cyclists.update = (): void => {};
const rules: any = new RuleMonitor(world, lanes, signals, traffic);
rules.strict = true;
if (VEHICLE === 'truck') rules.truck = true;
const v: any = new Vehicle(SPEC, new WorldGround(world, { heightAt: () => 100, coverAt: () => 0 }));
const smp = newPolySample();
const hit = newSurfaceHit();

// from the street between the junctions (0, 1) and (1, 1), eastwards – or (deadend) northwards up the last column, towards the cul-de-sac
const first = VARIANT === 'deadend' ? lanes.edgeLanes[town.street(opt.cols - 1, 0, opt.cols - 1, 1)].back : lanes.edgeLanes[town.street(0, 1, 1, 1)].fwd;
const start: any = first[0];
// (leftlane: the car stands in the lane beside the one its way begins on, where there is one – it has to get in lane)
lanePoint(VARIANT === 'leftlane' && first.length > 1 ? first[1] : start, 15, smp);
v.teleport(smp.x, 100 + 0.02, smp.z, Math.atan2(-smp.tx, -smp.tz));
v.resetDrivetrain(true);

// the way: turns in a fixed order, no lane twice
const PATTERN = [TURN.STRAIGHT, TURN.RIGHT, TURN.LEFT, TURN.STRAIGHT, TURN.LEFT, TURN.RIGHT, TURN.STRAIGHT, TURN.STRAIGHT, TURN.LEFT, TURN.RIGHT];
const route: any[] = [];
const taken = new Set<any>([start]);
for (let k = 0, l = start; k < 80; k++) {
  const want = PATTERN[k % PATTERN.length];
  const free = l.out.filter((q: any) => !taken.has(q.to));
  const c = free.find((q: any) => q.turn === want) ?? free.find((q: any) => q.turn !== TURN.UTURN) ?? free[0];
  if (!c) break;
  route.push(c);
  taken.add(c.to);
  l = c.to;
}
// (it ends on a street it has not been on before: the place to halt at is looked for by the street)
const streets = (upTo: number): Set<any> => new Set<any>([start.edge, ...route.slice(0, upTo).map((c) => c.to.edge)]);
while (route.length > 1 && streets(route.length - 1).has(route[route.length - 1].to.edge)) route.pop();
const driver: any = new AutoDriver(world, signals, rules, traffic, { spec: SPEC, front: FRONT, truck: VEHICLE === 'truck', obey: VARIANT !== 'reckless' });
const last: any = route[route.length - 1].to;
lanePoint(last, last.len * 0.6, smp);
driver.halt = { x: smp.x, z: smp.z };

const DT = 1 / 60;
const mpos = new Float64Array(3);
const out: string[] = [`route ${route.map((c) => `${c.from.id}>${c.to.id}`).join(' ')}`];
const steps = Math.round(SECONDS * 60);
let crashes = 0;
let turned = 0;
for (let i = 0; i < steps; i++) {
  v.getModelPosition(mpos);
  const lane = rules.laneNow;
  const laneS = rules.laneFront;
  const cmd = driver.step(DT, v, mpos, route);
  let standing = false;
  if (cmd.turnRound) {
    // the end of a street the way leads back out of: as in the game, the car is turned round on the spot
    const to = cmd.turnRound.to;
    lanePoint(to, Math.min(3, to.len / 2), smp);
    const ty2 = world.surfaceAt(smp.x, smp.z, smp.y + 1.5, hit) ? hit.y : smp.y;
    v.teleport(smp.x, ty2 + 0.05, smp.z, Math.atan2(-smp.tx, -smp.tz));
    driver.reset();
    turned++;
    rules.placed();
    standing = true;
  }
  const input = standing
    ? { steer: 0, throttle: 0, brake: 1, clutch: 0, handbrake: 0, gear: 1, ignition: true, starter: false }
    : { steer: cmd.steer, throttle: cmd.throttle, brake: cmd.brake, clutch: 0, handbrake: 0, gear: 1, ignition: true, starter: false };
  v.step(DT, input);
  signals.update(DT);
  v.getModelPosition(mpos);
  // (the traffic wants the middle of the vehicle's length)
  const mid = LENGTH / 2 - FRONT;
  traffic.update(DT, { x: mpos[0] + Math.sin(v.yaw) * mid, y: mpos[1], z: mpos[2] + Math.cos(v.yaw) * mid, yaw: v.yaw, speed: v.speed, length: LENGTH, width: WIDTH, lane, s: laneS - LENGTH / 2 });
  for (const e of traffic.events) if (e.type === 'collision') crashes++;
  rules.update(DT, { x: mpos[0], y: mpos[1], z: mpos[2], yaw: v.yaw, speed: v.speed, indicator: cmd.indicator, hazard: false, lookYaw: cmd.look, seatbelt: true, handbrake: 0, stalled: false, hitScenery: 0, length: LENGTH, front: FRONT }, traffic.events);
  if ((i + 1) % 6 !== 0) continue;
  const j = rules.context.junction;
  const s = driver.seen;
  out.push(
    `${i + 1} ${mpos[0]} ${mpos[1]} ${mpos[2]} ${v.yaw} ${v.speed} ${v.rpm} ${v.gear} ${cmd.steer} ${cmd.throttle} ${cmd.brake} ${cmd.indicator} ${cmd.look} ${cmd.remark || '-'} ${cmd.waiting ? 1 : 0} ${cmd.arrived ? 1 : 0} ` +
      `${rules.context.limit} ${j ? j.dist : -1} ${j ? j.signal : -9} ${j ? j.role : -9} ${rules.context.onRoad ? 1 : 0} ${rules.laneNow ? rules.laneNow.id : -1} ${rules.laneFront} ` +
      `${s.limit} ${s.distToLine} ${s.gap} ${s.mustStop ? 1 : 0} ${s.crossStop ? 1 : 0} ${s.turn} ${traffic.cars.length} ${rules.log.length} ${crashes} ${turned}`,
  );
}
for (const f of rules.log) out.push(`fault ${f.id} ${f.severity} ${f.time} ${f.x} ${f.z}`);
out.push(`end faults ${rules.log.length} points ${rules.points} crashes ${crashes} turned ${turned} arrived ${driver.cmd.arrived ? 1 : 0}`);
if (traffic.peds.length > 0 || traffic.cyclists.list.length > 0) throw new Error('people or cyclists turned up: the two runs are not alike');
process.stdout.write(out.join('\n') + '\n');
