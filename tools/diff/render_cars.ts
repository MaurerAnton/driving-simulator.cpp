// SPDX-License-Identifier: AGPL-3.0-only
/**
 * What the original makes of its cars and of the traffic's picture, printed as
 * numbers: the lines tests/render_car_test.cpp and tests/render_actors_test.cpp
 * expect from the port (they print the same themselves, and compare).
 *
 *   cd /home/user/driving-simulator
 *   node /path/to/port/tools/diff/render_cars.ts --part car    > /path/to/port/tests/fixtures/render-car.txt
 *   node /path/to/port/tools/diff/render_cars.ts --part actors > /path/to/port/tests/fixtures/render-actors.txt
 *
 * car: the body shells of the three styles (src/render/carBody.ts), the car of
 * the traffic and the player's car with its cockpit as trees of objects – every
 * mesh with its geometry and material –, what update(), the see-through cabin
 * and the mirrors change (src/render/car.ts, mirrors.ts), the instruments'
 * drawing (instruments.ts) and a vehicle's screen (navscreen.ts).
 * actors: the cars the traffic's picture keeps (src/render/actors.ts
 * TrafficRenderer) and the traffic lights' heads of a test town (SignalRenderer).
 *
 * The canvases are stand-ins that count what is drawn on them, as the port's
 * native host does.  Places are printed with 100 added, so that a relative
 * tolerance of the comparison is an absolute one for them (0.1 mm at 1e-6).
 */
import { resolve } from 'node:path';

const args = process.argv.slice(2);
const arg = (name: string, dflt: string): string => {
  const i = args.indexOf(`--${name}`);
  return i >= 0 && i + 1 < args.length ? args[i + 1] : dflt;
};
const root = resolve(arg('root', process.cwd()));
const part = arg('part', 'car');
const src = (path: string): string => `${root}/src/${path}`;

// ---- the canvases: every call and every property set is counted, the texts are kept ----
interface Rec {
  calls: number;
  texts: string[];
}
function makeCanvas(): unknown {
  const rec: Rec = { calls: 0, texts: [] };
  const canvas: Record<string, unknown> = { width: 0, height: 0, style: {}, rec };
  const ctx: unknown = new Proxy(
    {},
    {
      get: (_t, k) => {
        if (k === 'canvas') return canvas;
        if (k === 'measureText') return () => ({ width: 10 });
        return (...a: unknown[]) => {
          rec.calls++;
          if (k === 'fillText' || k === 'strokeText') rec.texts.push(String(a[0]));
          return ctx;
        };
      },
      set: () => {
        rec.calls++;
        return true;
      },
    },
  );
  canvas.getContext = () => ctx;
  return canvas;
}
(globalThis as { document?: unknown }).document = { createElement: () => makeCanvas() };

const THREE = await import(`${root}/node_modules/three/build/three.module.js`);
const { COMPACT_DIMS, buildCarShell, buildWheel, bodyProfile } = await import(src('render/carBody.ts'));

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
/** a number for a list of numbers that changes when one of them does, or their order – and does not cancel: Σ |v[i]| · (i mod 13 + 1) */
const absSum = (a: ArrayLike<number>, n = a.length): number => {
  let s = 0;
  for (let i = 0; i < n; i++) s += Math.abs(a[i]) * ((i % 13) + 1);
  return s;
};
/** a place */
const P = (v: number): number => v + 100;
const P3 = (v: { x: number; y: number; z: number }): number[] => [P(v.x), P(v.y), P(v.z)];

/* eslint-disable @typescript-eslint/no-explicit-any */
function geometry(g: any): void {
  const names = Object.keys(g.attributes);
  const index = g.getIndex();
  out('g', names.length, index ? index.count : -1, index ? absSum(index.array) : 0);
  for (const name of names) {
    const a = g.attributes[name];
    out('a', name, a.itemSize, a.count, absSum(a.array));
  }
  const position = g.attributes.position;
  if (position && position.count > 0) {
    g.computeBoundingBox();
    out('b', ...P3(g.boundingBox.min), ...P3(g.boundingBox.max));
  }
}

function material(m: any): void {
  const lit = m.type === 'MeshLambertMaterial' || m.type === 'MeshPhongMaterial';
  const phong = m.type === 'MeshPhongMaterial';
  out(
    'm',
    m.type,
    m.color.r,
    m.color.g,
    m.color.b,
    m.opacity,
    m.transparent,
    m.depthWrite,
    m.depthTest,
    m.side,
    m.shadowSide ?? -1,
    m.map ? 1 : 0,
    m.toneMapped,
    m.fog,
    m.polygonOffset,
    m.polygonOffsetFactor,
    m.polygonOffsetUnits,
    m.colorWrite,
    lit ? m.emissive.r : 0,
    lit ? m.emissive.g : 0,
    lit ? m.emissive.b : 0,
    phong ? m.specular.r : 0,
    phong ? m.specular.g : 0,
    phong ? m.specular.b : 0,
    phong ? m.shininess : 0,
  );
  // a picture painted on a canvas: its size, how much was drawn, the texts
  const rec: Rec | undefined = m.map?.image?.rec;
  if (rec) out('c', m.map.image.width, m.map.image.height, rec.calls, rec.texts.length, ...rec.texts);
}

/** An object and everything below it: where each is, and of a mesh its geometry and its material. */
function tree(name: string, top: any): void {
  out('tree', name);
  const visit = (o: any, depth: number): void => {
    out('o', depth, o.type, o.name, ...P3(o.position), P(o.quaternion.x), P(o.quaternion.y), P(o.quaternion.z), P(o.quaternion.w), o.scale.x, o.scale.y, o.scale.z, o.visible, o.renderOrder, o.castShadow, o.receiveShadow, o.frustumCulled, o.children.length);
    if (o.isMesh) {
      geometry(o.geometry);
      material(o.material);
      if (o.isInstancedMesh) out('n', o.count, absSum(o.instanceMatrix.array, o.count * 16), o.instanceColor ? absSum(o.instanceColor.array, o.count * 3) : -1);
    }
    for (const c of o.children) visit(c, depth + 1);
  };
  visit(top, 0);
}

/** The same objects after something was changed: where they are, whether they show, how their materials are set. */
function pose(name: string, top: any): void {
  out('pose', name);
  const visit = (o: any, depth: number): void => {
    const m = o.isMesh ? o.material : null;
    out('p', depth, ...P3(o.position), P(o.quaternion.x), P(o.quaternion.y), P(o.quaternion.z), P(o.quaternion.w), o.scale.x, o.scale.y, o.scale.z, o.visible, o.renderOrder, m ? m.transparent : '-', m ? m.opacity : '-', m ? m.color.r : '-', m ? m.color.g : '-', m ? m.color.b : '-');
    for (const c of o.children) visit(c, depth + 1);
  };
  visit(top, 0);
}

const STYLES = [COMPACT_DIMS, { ...COMPACT_DIMS, length: 4.6, height: 1.5, wheelbase: 2.7, tail: 1 }, { ...COMPACT_DIMS, length: 4.45, height: 1.42, wheelbase: 2.68, tail: 2 }];

if (part === 'car') {
  const { CarExterior, PlayerCar, cabinMaterial } = await import(src('render/car.ts'));
  const { Mirror, mirrorNormal } = await import(src('render/mirrors.ts'));
  const { InstrumentPanel } = await import(src('render/instruments.ts'));
  const { NavScreen } = await import(src('render/navscreen.ts'));

  // ---- the body shells, the wheels ----
  STYLES.forEach((d, i) => {
    const sh = buildCarShell(d);
    out('shell', i, sh.zFront, sh.zRear, sh.zAxleF, sh.zAxleR, sh.zCowl, sh.zRoofFront, sh.zRoofRear, sh.zPillarB, sh.zTail, sh.halfWidth, sh.beltY, sh.roofY, sh.cowlY, sh.cowlCrownY);
    out('nose', ...sh.noseOutline.flat());
    out('tail', ...sh.tailOutline.flat());
    for (const k of ['paint', 'glass', 'trim', 'lining', 'headliner', 'pillars', 'pillarTrim']) {
      out('part', k);
      geometry(sh[k]);
    }
    const at = bodyProfile(d);
    for (const z of [-2, -1.3, -0.5, 0, 0.7, 1.4, 2]) out('profile', z, ...at(z).flat());
    const w = buildWheel(d);
    out('wheel', i);
    geometry(w.tyre);
    geometry(w.rim);
  });

  // ---- the car of the traffic, in each style ----
  STYLES.forEach((d, i) => {
    const ex = new CarExterior(0x8a8f96, { dims: d });
    tree(`exterior ${i}`, ex.group);
    out('lists', ex.wheels.length, ex.wheelSpinners.length, ex.solids.length, ex.lampMeshes.length);
    ex.setWheels([0.3, 0.25, 0, 0], [1, 2, 3, 4], [0.31, 0.32, 0.33, 0.34]);
    ex.setLamps({ low: true, high: false, brake: true, reverse: false, left: true, right: false });
    pose(`exterior ${i} driving`, ex.group);
    ex.setLamps({ low: false, high: true, brake: false, reverse: true, left: false, right: true });
    pose(`exterior ${i} reversing`, ex.group);
    ex.setLamps({ low: false, high: false, brake: false, reverse: false, left: false, right: false });
    pose(`exterior ${i} dark`, ex.group);
  });
  // (with the mirror housings left out, a plate, glass one sees through, and materials of its own)
  tree('exterior plain', new CarExterior(0x203040, { plate: 'KS AB 123', seeThroughGlass: true, noMirrorHousings: true, ownMaterials: true }).group);
  for (const [hex, shine] of [[0x40444b, 0], [0xb9bec4, 50]]) {
    out('cabinMaterial', hex, shine);
    material(cabinMaterial(hex, shine));
  }

  // ---- the player's car ----
  const car = new PlayerCar();
  tree('car', car.root);
  out('eye', ...car.eye.toArray());
  out('doorTrim', car.doorTrim.length);
  for (const t of car.doorTrim) {
    out('trim', t.mesh.material.color.r, t.rest ? 1 : 0, t.doors.length);
    if (t.rest) geometry(t.rest);
    for (const g of t.doors) {
      out('door', g ? 1 : 0);
      if (g) geometry(g);
    }
  }
  out('mirrors', car.mirrors.length);
  for (const m of car.mirrors) {
    const s = m.spec;
    out('mirror', s.name, ...P3(s.centre), s.width, s.height, ...P3(s.normal), s.convex, s.resolution[0], s.resolution[1], m.target.width, m.target.height, m.enabled, m.attached.length);
  }
  out('screen', car.screenMesh.name, car.screenMesh.material.map === car.mapTexture ? 1 : 0, car.mapCanvas.width, car.mapCanvas.height);

  // ---- what the driver does, on the car ----
  const state = (over: object): object => ({
    speedKmh: 0, rpm: 800, gear: 0, indicator: 0, hazard: false, blinkOn: false, lowBeam: false, highBeam: false, handbrake: false, ignition: true, engineRunning: true, abs: false, seatbelt: true, fuel: 0.6, automatic: false, showGear: false, odometerKm: 0,
    steerWheel: 0, wheelSteer: [0, 0, 0, 0], wheelSpin: [0, 0, 0, 0], wheelY: [0.316, 0.316, 0.316, 0.316], clutch: 0, brake: 0, throttle: 0, handbrakeLever: 0, braking: false, reversing: false,
    ...over,
  });
  const cluster = (): Rec => car.cockpit.getObjectByName('cluster').material.map.image.rec;
  const steps: Array<[string, object, number, number]> = [
    ['third gear, turning left', state({ speedKmh: 47.6, rpm: 2300, gear: 3, indicator: -1, blinkOn: true, lowBeam: true, showGear: true, odometerKm: 12.34, steerWheel: -1.2, wheelSteer: [-0.21, -0.19, 0, 0], wheelSpin: [5, 6, 7, 8], wheelY: [0.31, 0.32, 0.316, 0.3], clutch: 0.2, throttle: 0.6 }), 0.016, 10],
    ['the same, a moment later', state({ speedKmh: 47.6, rpm: 2300, gear: 3, indicator: -1, blinkOn: true, lowBeam: true, showGear: true, odometerKm: 12.34, steerWheel: -1.2, clutch: 0.2, throttle: 0.6 }), 0.016, 10.02],
    ['braking in second, hazard lights', state({ speedKmh: 21, rpm: 1500, gear: 2, hazard: true, blinkOn: true, highBeam: true, lowBeam: true, abs: true, seatbelt: false, fuel: 0.1, brake: 0.8, braking: true, handbrakeLever: 0.5, handbrake: true }), 0.1, 11],
    ['neutral', state({ gear: 0, engineRunning: false }), 0.05, 12],
    ['reverse', state({ gear: -1, reversing: true, speedKmh: -3 }), 1, 13],
    ['sixth', state({ gear: 6, speedKmh: 131.5, rpm: 2900 }), 0.03, 14],
    ['an automatic in P', state({ automatic: true, gear: -2, ignition: false }), 0.2, 15],
    ['an automatic in D', state({ automatic: true, gear: 4, speedKmh: 80.5, indicator: 1, blinkOn: true }), 0.2, 16],
    ['a clock that jumped back', state({ automatic: true, gear: -1 }), 0.2, 3],
  ];
  for (const [name, s, dt, now] of steps) {
    car.update(s, dt, now);
    pose(name, car.root);
    const rec = cluster();
    out('cluster', rec.calls, rec.texts.length, ...rec.texts.slice(-12));
  }

  // ---- the cabin seen through, the map on the screen ----
  for (const o of [0.4, 0.4, 1, Number.NaN, 0.999, 0.01]) {
    car.setCabinOpacity(o);
    pose(`cabin ${o}`, car.root);
  }
  car.setMapOnScreen(true);
  out('screen', car.screenMesh.name, car.screenMesh.material.map === car.mapTexture ? 1 : 0, car.screenMesh.material.version);
  car.setMapOnScreen(true);
  car.setMapOnScreen(false);
  out('screen', car.screenMesh.name, car.screenMesh.material.map === car.mapTexture ? 1 : 0, car.screenMesh.material.version);

  // ---- the mirrors: where their cameras look, the glass put elsewhere ----
  const aimed = (m: any, ok: boolean): void => {
    out('aim', m.spec.name, ok, ...m.camera.matrix.elements.map(P), ...m.camera.projectionMatrix.elements);
  };
  const moved = new THREE.Matrix4().makeRotationY(0.7).setPosition(30, 2, -40);
  for (const m of car.mirrors) {
    aimed(m, m.aim(new THREE.Matrix4(), car.eye, 300));
    aimed(m, m.aim(moved, car.eye.clone().applyMatrix4(moved), 450, 1.5));
    aimed(m, m.aim(moved, car.eye.clone().applyMatrix4(moved), 450, 1, 2, Math.tan((7 * Math.PI) / 180)));
    out('behind', m.spec.name, m.aim(new THREE.Matrix4(), m.spec.centre.clone().addScaledVector(m.spec.normal, -0.5), 300));
    const housing = m.makeHousing(0.05, new THREE.MeshBasicMaterial());
    out('housing', m.spec.name);
    geometry(housing.geometry);
  }
  const placed = (m: any, name: string): void => {
    out('placed', name, ...P3(m.spec.centre), m.spec.width, m.spec.height, ...P3(m.spec.normal));
    for (const o of [m.mesh, ...m.attached]) out('at', ...P3(o.position), P(o.quaternion.x), P(o.quaternion.y), P(o.quaternion.z), P(o.quaternion.w), o.scale.x, o.scale.y, o.scale.z, ...o.matrix.elements.map(P));
  };
  const inside = car.mirrors[0];
  inside.place(0.05, 1.2, car.eye);
  placed(inside, 'shifted, larger, turned to the eye');
  aimed(inside, inside.aim(new THREE.Matrix4(), car.eye, 300));
  inside.place(-0.1, 0.8, null);
  placed(inside, 'shifted the other way, smaller');
  inside.place(0, 1, null);
  placed(inside, 'back');
  for (const m of car.mirrors) {
    m.setEnabled(false);
    out('off', m.spec.name, m.enabled, m.mesh.material.map ? 1 : 0, m.mesh.material.color.r, m.mesh.material.color.g, m.mesh.material.color.b);
    m.setEnabled(true);
    out('on', m.spec.name, m.enabled, m.mesh.material.map === m.target.texture ? 1 : 0, m.mesh.material.color.r);
  }
  {
    const eye = new THREE.Vector3(-0.36, 1.2, 0.2);
    const centre = new THREE.Vector3(0.995, 1.0163, -0.675);
    out('mirrorNormal', ...P3(mirrorNormal(centre, eye, new THREE.Vector3(0.16, -0.06, 1))));
    const m = new Mirror({ name: 'test', centre, width: 0.17, height: 0.105, normal: mirrorNormal(centre, eye, new THREE.Vector3(0.16, -0.06, 1)), convex: 1.6, resolution: [16, 16] });
    tree('mirror', m.mesh);
  }

  // ---- the instruments by themselves ----
  {
    const panel = new InstrumentPanel();
    const rec: Rec = (panel.canvas as unknown as { rec: Rec }).rec;
    out('panel', panel.canvas.width, panel.canvas.height, rec.calls, rec.texts.length);
    const shows: Array<[object, number]> = [
      [state({ ignition: false }), 1],
      [state({ speedKmh: 99.5, rpm: 6900, gear: 5, showGear: true, odometerKm: 1234.56, fuel: 0.05, lowBeam: true, highBeam: true, handbrake: true, abs: true, engineRunning: false, seatbelt: false, hazard: true, blinkOn: true }), 2],
      [state({ speedKmh: 12, automatic: true, gear: 0 }), 2.01],
      [state({ speedKmh: 12, automatic: true, gear: -1, indicator: 1, blinkOn: true, fuel: 2 }), 2.1],
      [state({ speedKmh: -4, automatic: true, gear: 3, fuel: -1 }), -5],
    ];
    for (const [s, now] of shows) {
      const before = rec.texts.length;
      panel.update(s, now);
      out('shown', rec.calls, ...rec.texts.slice(before));
    }
  }

  // ---- a vehicle's screen ----
  {
    const parent = new THREE.Group();
    const nav = new NavScreen(0.27).place(parent, new THREE.Vector3(0.62, 1.5, -1.2), new THREE.Vector3(-0.4, 1.9, -0.6), new THREE.Vector3(0.62, 1.3, -1.25));
    tree('nav screen', parent);
    out('nav', nav.canvas.width, nav.canvas.height, nav.hitMesh().name);
    nav.setLive(true);
    material(nav.hitMesh().material);
    nav.setLive(false);
    material(nav.hitMesh().material);
    const bare = new THREE.Group();
    new NavScreen(0.22).place(bare, new THREE.Vector3(0, 1, 0), new THREE.Vector3(0, 1, 2));
    tree('nav screen without an arm', bare);
  }
}

if (part === 'actors') {
  const { TrafficRenderer, SignalRenderer } = await import(src('render/actors.ts'));
  const { SignalSystem, SIG } = await import(src('sim/signals.ts'));
  const { XING } = await import(src('world/types.ts'));
  const { makeTown } = await import(`${root}/tests/game/town.ts`);

  // ---- the cars of the traffic ----
  {
    const traffic = new TrafficRenderer();
    const carOf = (id: number, style: number, color: number, x: number, over: object = {}): object => ({ id, style, color, x, y: 100 + id * 0.01, z: -3 * id, yaw: 0.1 * id, pitch: 0.01 * id, wheelSpin: id * 1.5, blinker: 0, braking: false, v: 5, ...over });
    /** the cars' models: what hangs in the group besides the people and the emergency vehicles (which are drawn by other parts) */
    const shown = (name: string): void => {
      const models = traffic.group.children.filter((c: any) => c !== traffic.people.group && c.name !== 'emergency' && c.children.length > 0 && c.children[0].isMesh && c.children.length > 8);
      out('traffic', name, models.length);
      for (const g of models) pose('car', g);
    };
    const a = [carOf(1, 0, 0xd9dadc, 10), carOf(2, 1, 0x1b3fa0, 20, { blinker: -1, braking: true }), carOf(3, 2, 0x8a1c1c, 30, { blinker: 1, drawn: { x: 30.5, z: -9.5, yaw: 0.35, steer: 0.2 } }), carOf(4, 0, 0x202428, 40, { drawn: { x: 40.25, z: -12.5, yaw: 0.45 } })];
    traffic.update(a, [], 0);
    shown('four cars');
    traffic.highOf = (id: number) => id === 2;
    traffic.update(a, [], 0.4);
    shown('the indicators off, a main beam');
    traffic.highOf = null;
    // two leave, one comes: it takes the model of one that left (the same style), in its own paint
    const b = [a[1], a[2], carOf(7, 0, 0x35c06a, 70)];
    traffic.update(b, [], 1);
    shown('two left, one came');
    traffic.update([carOf(8, 0, 0xffb020, 80), carOf(9, 0, 0x3d8bff, 90), carOf(10, 2, 0x111111, 100)], [], 2.7);
    shown('all others');
    traffic.update([], [], 3);
    shown('none');
    traffic.update(a, [], 4);
    shown('the first four again');
  }

  // ---- the traffic lights' heads ----
  const heads = (name: string, opt: object, arrowAt: number[] | null, lightsOut: ((c: number) => boolean) | undefined): void => {
    out('town', name);
    const town = makeTown({ ...opt, lightsOut });
    const world = town.world;
    const signals = new SignalSystem(world.signals, lightsOut);
    // (a green arrow, hung by hand beside the head of an approach: sim/arrows.ts would work out where one belongs)
    let arrowArm: any = null;
    if (arrowAt) {
      const node = world.nodes[town.node(arrowAt[0], arrowAt[1])];
      arrowArm = node.arms.find((arm: any) => arm.isApproach && arm.lamp === arrowAt[2]);
      signals.arrows.set(arrowArm, { controller: node.sc, own: arrowArm.lamp, foes: [], friends: [], state: -1, since: 0 });
    }
    const r = new SignalRenderer(world, signals);
    const all = (): void => {
      out('spots', r.spots.length, r.poles.length, r.group.children.length);
      for (const s of r.spots) out('spot', P(s.x), P(s.y), P(s.z), P(s.yaw), s.controller, s.group, s.arrow ? 1 : 0);
      out('poles', absSum(r.poles));
      tree('heads', r.group);
    };
    all();
    const lit = (what: string): void => {
      r.update(signals);
      out('lit', what, ...r.group.children.map((m: any) => (m.instanceColor ? absSum(m.instanceColor.array, m.count * 3) : -1)), ...r.group.children.map((m: any) => (m.instanceColor ? m.instanceColor.version : -1)));
    };
    lit('at the start');
    lit('nothing changed');
    for (const t of [0.3, 0.3, 27.5, 3, 1, 1, 9, 9.25, 3.5]) {
      signals.update(t);
      lit(`after ${t} s`);
    }
    if (arrowArm) {
      signals.arrows.get(arrowArm).state = SIG.GREEN;
      lit('the arrow green');
      signals.arrows.get(arrowArm).state = SIG.AMBER;
      lit('the arrow amber');
      signals.arrows.get(arrowArm).state = -1;
      lit('the arrow dark');
    }
    // set up anew (the network comes in pieces): the same heads, no arrow without the signals
    r.rebuild(world);
    all();
    r.rebuild(world, signals);
    out('rebuilt', r.spots.length, r.poles.length, r.group.children.length);
  };
  const opt = {
    cols: 4,
    rows: 3,
    pavement: 2,
    deadEnd: 60,
    wide: [[1, 1, 2, 1]],
    oneWay: [[0, 0, 1, 0]],
    lights: [[1, 1], [2, 1], [1, 0]],
    crossings: [
      { street: [2, 0, 3, 0], at: 0.5, k: XING.ZEBRA },
      { street: [1, 2, 2, 2], at: 0.4, k: XING.SIGNAL },
      { street: [2, 1, 3, 1], at: 0.2, k: XING.SIGNAL },
      { street: [0, 0, 1, 0], at: 0.3, k: XING.SIGNAL },
    ],
  };
  heads('with pavements, an arrow', opt, [1, 1, 1], undefined);
  heads('one controller out of order', opt, null, (c: number) => c === 0);
  heads('no pavements, no lights at junctions', { cols: 3, rows: 2, crossings: [{ street: [0, 0, 1, 0], at: 0.5, k: XING.SIGNAL }] }, null, undefined);
  heads('no lights at all', { cols: 2, rows: 2 }, null, undefined);
}

process.stdout.write(lines.join('\n') + '\n');
