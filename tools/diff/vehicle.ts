// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The original's vehicle model on scripted drives, for comparison with tools/dev/vehicle_dump.cpp:
 *
 *   node tools/diff/vehicle.ts [path of the original] > a.txt        (default: the current directory)
 *   tools/t.sh vehicle_dump > b.txt
 *   tools/diff/compare.py a.txt b.txt
 *
 * Nine vehicles (the compact car with manual gearbox, clutch help and automatic, truck, bus, tractor,
 * motorcycle manual and automatic, moped) each drive ten scripts on the plane of the original's test
 * harness: a standing start at full throttle, braking from speed, a slalom, a hill start, reversing,
 * a kerb, a roll-over, starting and stalling the engine, parking on a slope, and ice.  Every 0.1 s a
 * line of state is printed with full precision.  The two programs must print the same numbers:
 * keep the scripts here and in vehicle_dump.cpp the same, line by line.
 */
import { join } from 'node:path';
import { pathToFileURL } from 'node:url';
import { strictMath } from './sim_math.ts';

strictMath();

const ROOT = process.argv[2] ?? process.cwd();
const at = (p: string): string => pathToFileURL(join(ROOT, p)).href;
const { Vehicle, COMPACT_CAR, COMPACT_CAR_AUTOCLUTCH, COMPACT_CAR_AUTOMATIC, TRUCK } = await import(at('src/sim/vehicle/index.ts'));
const { BUS, TRACTOR, MOTORCYCLE, MOTORCYCLE_AUTOMATIC, MOPED } = await import(at('src/sim/vehicle/fleet.ts'));
const { PlaneGround, input, DT } = await import(at('tests/vehicle/harness.ts'));

/* eslint-disable @typescript-eslint/no-explicit-any */
type Spec = any;
type Car = any;
type Input = any;

interface Entry {
  id: string;
  spec: Spec;
  /** from which speed (km/h) the brake test brakes, and at which the slalom is driven */
  vBrake: number;
  vSlalom: number;
}

const CARS: Entry[] = [
  { id: 'compact', spec: COMPACT_CAR, vBrake: 100, vSlalom: 50 },
  { id: 'compact-autoclutch', spec: COMPACT_CAR_AUTOCLUTCH, vBrake: 100, vSlalom: 50 },
  { id: 'compact-automatic', spec: COMPACT_CAR_AUTOMATIC, vBrake: 100, vSlalom: 50 },
  { id: 'truck', spec: TRUCK, vBrake: 60, vSlalom: 35 },
  { id: 'bus', spec: BUS, vBrake: 60, vSlalom: 35 },
  { id: 'tractor', spec: TRACTOR, vBrake: 30, vSlalom: 25 },
  { id: 'motorcycle', spec: MOTORCYCLE, vBrake: 100, vSlalom: 50 },
  { id: 'motorcycle-automatic', spec: MOTORCYCLE_AUTOMATIC, vBrake: 100, vSlalom: 50 },
  { id: 'moped', spec: MOPED.manual, vBrake: 40, vSlalom: 30 },
];

/** The test driver: pulls away and shifts up at full throttle, holds a gear, brakes. */
class Driver {
  gear = 1;
  shiftT = -1;
  readonly spec: Spec;
  readonly auto: boolean;
  readonly shiftRpm: number;

  constructor(spec: Spec) {
    this.spec = spec;
    this.auto = spec.transmission === 'automatic';
    this.shiftRpm = spec.limiterRpm - 300;
  }

  sprint(t: number, v: Car): Input {
    if (this.auto) return input({ gear: 1, throttle: 1 });
    if (t < 0.25) return input({ gear: 1, clutch: 1, throttle: 0.55 });
    if (this.shiftT >= 0) {
      if (t - this.shiftT < 0.25) return input({ gear: this.gear, clutch: 1, throttle: 0 });
      this.shiftT = -1;
    }
    if (v.rpm > this.shiftRpm && this.gear < this.spec.gearRatios.length) {
      this.gear++;
      this.shiftT = t;
      return input({ gear: this.gear, clutch: 1, throttle: 0 });
    }
    // slip the clutch off the line, then full power
    const launch = t < 1.6 ? Math.max(0, 0.62 - (t - 0.25) * 0.5) : 0;
    return input({ gear: this.gear, clutch: launch, throttle: 1 });
  }

  hold(throttle: number): Input {
    return input({ gear: this.auto ? 1 : this.gear, throttle });
  }

  brake(b: number): Input {
    return input({ gear: this.auto ? 1 : this.gear, brake: b, clutch: this.auto ? 0 : 1 });
  }
}

interface Script {
  name: string;
  seconds: number;
  ground: any;
  running: boolean;
  drive: (t: number, v: Car) => Input;
  /** something done to the vehicle before step `i` */
  before?: (i: number, v: Car) => void;
}

function scripts(car: Entry): Script[] {
  const s = car.spec;
  const auto = s.transmission === 'automatic';
  const list: Script[] = [];
  {
    const d = new Driver(s);
    list.push({ name: 'launch', seconds: 20, ground: new PlaneGround(), running: true, drive: (t, v) => d.sprint(t, v) });
  }
  {
    const d = new Driver(s);
    let braking = false;
    list.push({
      name: 'brake',
      seconds: 30,
      ground: new PlaneGround(),
      running: true,
      drive: (t, v) => {
        if (!braking && v.speed * 3.6 >= car.vBrake) braking = true;
        return braking ? d.brake(1) : d.sprint(t, v);
      },
    });
  }
  {
    const d = new Driver(s);
    let t0 = -1;
    list.push({
      name: 'slalom',
      seconds: 25,
      ground: new PlaneGround(),
      running: true,
      drive: (t, v) => {
        if (t0 < 0 && v.speed * 3.6 >= car.vSlalom) t0 = t;
        if (t0 < 0) return d.sprint(t, v);
        const inp = d.hold(0.3);
        inp.steer = (t - t0 < 8 ? 0.2 : 0.6) * Math.sin((t - t0) * 2.2);
        return inp;
      },
    });
  }
  list.push({
    name: 'hill',
    seconds: 10,
    ground: new PlaneGround(0.12),
    running: true,
    drive: (t) => {
      if (auto) return t < 1 ? input({ gear: 1, brake: 1 }) : input({ gear: 1, throttle: 0.5 });
      if (t < 1) return input({ gear: 1, clutch: 1, handbrake: 1 });
      if (t < 4) return input({ gear: 1, clutch: Math.max(0, 1 - (t - 1) * 0.4), throttle: 0.45, handbrake: t < 1.8 ? 1 : 0 });
      return input({ gear: 1, throttle: 0.5 });
    },
  });
  list.push({
    name: 'reverse',
    seconds: 8,
    ground: new PlaneGround(),
    running: true,
    drive: (t) => {
      const steer = t > 2 ? 0.4 : 0;
      if (t >= 5) return input({ gear: -1, brake: 0.6, clutch: auto ? 0 : 1, steer });
      if (auto) return input({ gear: -1, throttle: 0.3, steer });
      if (t < 0.3) return input({ gear: -1, clutch: 1 });
      return input({ gear: -1, clutch: Math.max(0, 0.7 - (t - 0.3) * 0.5), throttle: 0.3, steer });
    },
  });
  {
    const d = new Driver(s);
    const ground = new PlaneGround();
    ground.stepZ = -12;
    ground.stepHeight = 0.1;
    list.push({
      name: 'kerb',
      seconds: 6,
      ground,
      running: true,
      drive: (t, v) => {
        const inp = d.sprint(t, v);
        inp.steer = 0.03;
        return inp;
      },
    });
  }
  {
    const d = new Driver(s);
    list.push({
      name: 'roll',
      seconds: 10,
      ground: new PlaneGround(),
      running: true,
      drive: (t, v) => (t < 4 ? d.sprint(t, v) : input({ gear: auto ? 1 : 0 })),
      before: (i, v) => {
        // a blow from the left, above the centre of mass
        if (i === 960) v.applyImpulse(v.pos[0], v.pos[1] + 0.6, v.pos[2], s.mass * 9, 0, 0);
      },
    });
  }
  list.push({
    name: 'start',
    seconds: 6,
    ground: new PlaneGround(),
    running: false,
    drive: (t) => {
      if (t < 0.2) return input();
      if (t < 1.6) return input({ starter: true });
      if (auto) return t < 4 ? input({ gear: 1 }) : input({ gear: 1, ignition: false });
      if (t < 3) return input();
      if (t < 3.5) return input({ gear: 3, clutch: 1 });
      return input({ gear: 3, handbrake: 1 });
    },
  });
  list.push({
    name: 'park',
    seconds: 7,
    ground: new PlaneGround(0.1),
    running: auto,
    drive: (t) => {
      if (auto) return t < 1 ? input({ gear: -2 }) : t < 3 ? input({ gear: -1, throttle: 0.3 }) : input({ gear: -2 });
      if (t < 2) return input({ gear: 1 });
      if (t < 4) return input({ gear: 1, clutch: 1 });
      return input({ gear: 1, clutch: 1, handbrake: 1 });
    },
  });
  {
    const d = new Driver(s);
    list.push({
      name: 'ice',
      seconds: 12,
      ground: new PlaneGround(0, 0.3),
      running: true,
      drive: (t, v) => {
        if (t < 6) return d.sprint(t, v);
        const inp = d.brake(1);
        inp.steer = 0.3;
        return inp;
      },
    });
  }
  return list;
}

const out: string[] = [];
const flag = (b: boolean): number => (b ? 1 : 0);
for (const car of CARS) {
  for (const sc of scripts(car)) {
    const v = new Vehicle(car.spec, sc.ground);
    v.teleport(0, 0, 0, 0);
    v.resetDrivetrain(sc.running);
    const n = Math.round(sc.seconds / DT);
    for (let i = 0; i < n; i++) {
      const t = i * DT;
      sc.before?.(i, v);
      v.step(DT, sc.drive(t, v));
      if ((i + 1) % 24 !== 0) continue;
      const w = v.wheels;
      const f: number[] = [
        i + 1,
        v.pos[0], v.pos[1], v.pos[2],
        v.vel[0], v.vel[1], v.vel[2],
        v.yaw, v.rpm, v.gear,
        w[0].load, w[1].load, w[2].load, w[3].load,
        v.quat[0], v.quat[1], v.quat[2], v.quat[3],
        v.angVel[0], v.angVel[1], v.angVel[2],
        w[0].omega, w[1].omega, w[2].omega, w[3].omega,
        w[0].compression, w[1].compression, w[2].compression, w[3].compression,
        v.speed, v.accLong, v.accLat, v.steerTorque, v.clutchLock, v.engineLoad, v.clutchPedal, v.odometer, v.upright,
        flag(v.engineRunning), flag(v.absActive), flag(v.bodyContact), flag(v.stalledEvent), flag(v.gearGrind),
      ];
      out.push(`${car.id}/${sc.name} ${f.join(' ')}`);
    }
  }
}
process.stdout.write(out.join('\n') + '\n');
