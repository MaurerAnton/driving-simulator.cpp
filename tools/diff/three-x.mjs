// SPDX-License-Identifier: AGPL-3.0-only
// three.js itself, asked what the engine is asked in tools/dev/three_x_dump.cpp: the two
// print the same lines, tools/diff/compare.py compares them.
//
//   node tools/diff/three-x.mjs [math] [scene] [geom] > /tmp/a.txt      (no section named: all)
//   tools/t.sh three_x_dump [math] [scene] [geom] > /tmp/b.txt
//   tools/diff/compare.py /tmp/a.txt /tmp/b.txt
//
// three.js is taken from the original's node_modules (THREE_DIR names another place).
// Keep the two files alike, section by section and line by line: the random numbers
// are drawn in the same order on both sides.

const THREE_DIR = process.env.THREE_DIR || '/home/user/driving-simulator/node_modules/three';
// (three.js warns about texts that are no colours and the like: they are asked on purpose)
console.warn = () => {};
const THREE = await import(THREE_DIR + '/build/three.module.js');
const { mergeGeometries, mergeVertices } = await import(THREE_DIR + '/examples/jsm/utils/BufferGeometryUtils.js');

// ---------------------------------------------------------------------------
// printing
// ---------------------------------------------------------------------------

const lines = [];
function num(x) {
  if (Object.is(x, -0)) return '0';
  return String(x);
}
function word(x) {
  if (typeof x === 'number') return num(x);
  if (typeof x === 'boolean') return x ? '1' : '0';
  if (x === null || x === undefined) return 'null';
  if (typeof x === 'string') return x === '' ? '""' : x.replace(/\s+/g, '_');
  if (Array.isArray(x) || ArrayBuffer.isView(x)) return Array.from(x, word).join(' ');
  if (x.isVector2) return `${num(x.x)} ${num(x.y)}`;
  if (x.isVector3) return `${num(x.x)} ${num(x.y)} ${num(x.z)}`;
  if (x.isVector4 || x.isQuaternion) return `${num(x.x)} ${num(x.y)} ${num(x.z)} ${num(x.w)}`;
  if (x.isEuler) return `${num(x.x)} ${num(x.y)} ${num(x.z)} ${x.order}`;
  if (x.isMatrix3 || x.isMatrix4) return x.elements.map(num).join(' ');
  if (x.isColor) return `${num(x.r)} ${num(x.g)} ${num(x.b)}`;
  if (x.isBox2 || x.isBox3) return word(x.min) + ' ' + word(x.max);
  if (x.isSphere) return word(x.center) + ' ' + num(x.radius);
  if (x.isPlane) return word(x.normal) + ' ' + num(x.constant);
  if (x instanceof THREE.Ray) return word(x.origin) + ' ' + word(x.direction);
  if (x instanceof THREE.Line3) return word(x.start) + ' ' + word(x.end);
  if (x instanceof THREE.Triangle) return word(x.a) + ' ' + word(x.b) + ' ' + word(x.c);
  if (x instanceof THREE.Spherical) return `${num(x.radius)} ${num(x.phi)} ${num(x.theta)}`;
  if ('h' in x && 's' in x && 'l' in x) return `${num(x.h)} ${num(x.s)} ${num(x.l)}`;
  throw new Error('cannot print ' + x);
}
function P(label, ...xs) {
  lines.push(label + ' ' + xs.map(word).join(' '));
}

// ---------------------------------------------------------------------------
// the random numbers (mulberry32, as fs::Rng) and what is made of them
// ---------------------------------------------------------------------------

let seedState = 1;
function seed(s) {
  seedState = s >>> 0;
}
function rnd() {
  seedState = (seedState + 0x6d2b79f5) | 0;
  let t = seedState;
  t = Math.imul(t ^ (t >>> 15), t | 1);
  t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
  return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
}
const r = (lo = -10, hi = 10) => lo + rnd() * (hi - lo);
const ri = (lo, hi) => lo + Math.floor(rnd() * (hi - lo + 1));
const V2 = (lo = -10, hi = 10) => { const x = r(lo, hi), y = r(lo, hi); return new THREE.Vector2(x, y); };
const V3 = (lo = -10, hi = 10) => { const x = r(lo, hi), y = r(lo, hi), z = r(lo, hi); return new THREE.Vector3(x, y, z); };
const V4 = (lo = -10, hi = 10) => { const x = r(lo, hi), y = r(lo, hi), z = r(lo, hi), w = r(lo, hi); return new THREE.Vector4(x, y, z, w); };
const Q = () => { const x = r(-1, 1), y = r(-1, 1), z = r(-1, 1), w = r(-1, 1); return new THREE.Quaternion(x, y, z, w).normalize(); };
const ORDERS = ['XYZ', 'YXZ', 'ZXY', 'ZYX', 'YZX', 'XZY'];
const E = (order = 'XYZ') => { const x = r(-Math.PI, Math.PI), y = r(-Math.PI, Math.PI), z = r(-Math.PI, Math.PI); return new THREE.Euler(x, y, z, order); };
/** a position, a rotation and a scale in one matrix */
const M4 = () => { const p = V3(), q = Q(), s = V3(0.5, 2); return new THREE.Matrix4().compose(p, q, s); };
/** sixteen numbers */
const M4any = () => { const a = []; for (let i = 0; i < 16; i++) a.push(r(-2, 2)); return new THREE.Matrix4().fromArray(a); };
const M3any = () => { const a = []; for (let i = 0; i < 9; i++) a.push(r(-2, 2)); return new THREE.Matrix3().fromArray(a); };
const C = () => { const x = rnd(), y = rnd(), z = rnd(); return new THREE.Color(x, y, z); };
const B3 = () => { const a = V3(), b = V3(); return new THREE.Box3().setFromPoints([a, b]); };
const B2 = () => { const a = V2(), b = V2(); return new THREE.Box2().setFromPoints([a, b]); };
const SPH = () => { const c = V3(), rad = r(0.5, 6); return new THREE.Sphere(c, rad); };
const PL = () => { const n = V3(-1, 1).normalize(), c = r(-5, 5); return new THREE.Plane(n, c); };
const RAY = () => { const o = V3(), d = V3(-1, 1).normalize(); return new THREE.Ray(o, d); };

// ---------------------------------------------------------------------------
// maths
// ---------------------------------------------------------------------------

function mathSection() {
  seed(20261124);

  // --- MathUtils
  for (let i = 0; i < 6; i++) {
    const x = r(), a = r(), b = r(), t = rnd();
    P('mu.clamp', THREE.MathUtils.clamp(x, -3, 4), THREE.MathUtils.clamp(x, a, b));
    P('mu.euclideanModulo', THREE.MathUtils.euclideanModulo(x, 3), THREE.MathUtils.euclideanModulo(a, 0.7));
    P('mu.mapLinear', THREE.MathUtils.mapLinear(x, a, b, 2, 5));
    P('mu.inverseLerp', THREE.MathUtils.inverseLerp(a, b, x), THREE.MathUtils.inverseLerp(a, a, x));
    P('mu.lerp', THREE.MathUtils.lerp(a, b, t));
    P('mu.damp', THREE.MathUtils.damp(a, b, 3.5, t));
    P('mu.pingpong', THREE.MathUtils.pingpong(x), THREE.MathUtils.pingpong(x, 2.5));
    P('mu.smoothstep', THREE.MathUtils.smoothstep(x, -5, 5), THREE.MathUtils.smoothstep(x, a, b), THREE.MathUtils.smootherstep(x, -5, 5));
    P('mu.deg', THREE.MathUtils.degToRad(x * 18), THREE.MathUtils.radToDeg(x));
    const n = ri(1, 5000);
    P('mu.pow2', n, THREE.MathUtils.isPowerOfTwo(n), THREE.MathUtils.isPowerOfTwo(1 << (n % 20)), THREE.MathUtils.ceilPowerOfTwo(n), THREE.MathUtils.floorPowerOfTwo(n));
  }
  P('mu.seededRandom', THREE.MathUtils.seededRandom(42), THREE.MathUtils.seededRandom(), THREE.MathUtils.seededRandom(), THREE.MathUtils.seededRandom(7));
  for (const order of ['XYX', 'YZY', 'ZXZ', 'XZX', 'YXY', 'ZYZ']) {
    const a = r(-3, 3), b = r(-3, 3), c = r(-3, 3);
    const q = new THREE.Quaternion();
    THREE.MathUtils.setQuaternionFromProperEuler(q, a, b, c, order);
    P('mu.properEuler', order, q);
  }

  // --- Vector2
  for (let i = 0; i < 6; i++) {
    const a = V2(), b = V2(), s = r(-3, 3), t = rnd();
    const m = M3any();
    const lo = a.clone().min(b), hi = a.clone().max(b);
    const p = V2(-20, 20);
    P('v2.add', a.clone().add(b), a.clone().addScalar(s), new THREE.Vector2().addVectors(a, b), a.clone().addScaledVector(b, s));
    P('v2.sub', a.clone().sub(b), a.clone().subScalar(s), new THREE.Vector2().subVectors(a, b));
    P('v2.mul', a.clone().multiply(b), a.clone().multiplyScalar(s), a.clone().divide(b), a.clone().divideScalar(s));
    P('v2.applyMatrix3', a.clone().applyMatrix3(m));
    P('v2.minmax', lo, hi, p.clone().clamp(lo, hi), p.clone().clampScalar(-2.5, 3.5), p.clone().clampLength(2, 9));
    P('v2.round', a.clone().floor(), a.clone().ceil(), a.clone().round(), a.clone().roundToZero(), a.clone().negate());
    P('v2.dot', a.dot(b), a.cross(b), a.lengthSq(), a.length(), a.manhattanLength());
    P('v2.normalize', a.clone().normalize(), a.clone().setLength(s), a.angle(), a.angleTo(b));
    P('v2.distance', a.distanceTo(b), a.distanceToSquared(b), a.manhattanDistanceTo(b));
    P('v2.lerp', a.clone().lerp(b, t), new THREE.Vector2().lerpVectors(a, b, t), a.equals(b), a.equals(a.clone()));
    P('v2.rotateAround', a.clone().rotateAround(b, s));
    P('v2.misc', a.clone().setX(s), a.clone().setY(s), a.clone().setScalar(s), a.clone().setComponent(1, s), a.getComponent(0), a.getComponent(1), a.width, a.height);
    P('v2.array', new THREE.Vector2().fromArray([s, t, 3], 1), a.toArray());
  }
  P('v2.zero', new THREE.Vector2().normalize(), new THREE.Vector2().angleTo(new THREE.Vector2(1, 0)), new THREE.Vector2().clampLength(1, 2), new THREE.Vector2(0, 0).angle());

  // --- Vector3
  for (let i = 0; i < 8; i++) {
    const a = V3(), b = V3(), s = r(-3, 3), t = rnd();
    const m4 = M4(), m4p = M4any(), m3 = M3any(), q = Q(), e = E(ORDERS[i % 6]);
    const lo = a.clone().min(b), hi = a.clone().max(b);
    const p = V3(-20, 20), n = V3(-1, 1).normalize();
    P('v3.add', a.clone().add(b), a.clone().addScalar(s), new THREE.Vector3().addVectors(a, b), a.clone().addScaledVector(b, s));
    P('v3.sub', a.clone().sub(b), a.clone().subScalar(s), new THREE.Vector3().subVectors(a, b));
    P('v3.mul', a.clone().multiply(b), a.clone().multiplyScalar(s), new THREE.Vector3().multiplyVectors(a, b), a.clone().divide(b), a.clone().divideScalar(s));
    P('v3.applyEuler', a.clone().applyEuler(e), a.clone().applyAxisAngle(n, s));
    P('v3.applyMatrix3', a.clone().applyMatrix3(m3), a.clone().applyNormalMatrix(m3));
    P('v3.applyMatrix4', a.clone().applyMatrix4(m4), a.clone().applyMatrix4(m4p));
    P('v3.applyQuaternion', a.clone().applyQuaternion(q), a.clone().transformDirection(m4));
    P('v3.minmax', lo, hi, p.clone().clamp(lo, hi), p.clone().clampScalar(-2.5, 3.5), p.clone().clampLength(2, 9));
    P('v3.round', a.clone().floor(), a.clone().ceil(), a.clone().round(), a.clone().roundToZero(), a.clone().negate());
    P('v3.dot', a.dot(b), a.lengthSq(), a.length(), a.manhattanLength());
    P('v3.normalize', a.clone().normalize(), a.clone().setLength(s));
    P('v3.lerp', a.clone().lerp(b, t), new THREE.Vector3().lerpVectors(a, b, t));
    P('v3.cross', a.clone().cross(b), new THREE.Vector3().crossVectors(a, b));
    P('v3.project', a.clone().projectOnVector(b), a.clone().projectOnPlane(n), a.clone().reflect(n));
    P('v3.angleTo', a.angleTo(b), a.distanceTo(b), a.distanceToSquared(b), a.manhattanDistanceTo(b));
    P('v3.spherical', new THREE.Vector3().setFromSphericalCoords(s, t * 3, a.x), new THREE.Vector3().setFromCylindricalCoords(s, a.x, a.y));
    P('v3.fromMatrix', new THREE.Vector3().setFromMatrixPosition(m4), new THREE.Vector3().setFromMatrixScale(m4), new THREE.Vector3().setFromMatrixColumn(m4, 1), new THREE.Vector3().setFromMatrix3Column(m3, 2));
    P('v3.from', new THREE.Vector3().setFromEuler(e), new THREE.Vector3().setFromColor(new THREE.Color(0.25, 0.5, 0.75)), a.equals(b), a.equals(a.clone()));
    P('v3.misc', a.clone().setX(s), a.clone().setY(s), a.clone().setZ(s), a.clone().setScalar(s), a.clone().setComponent(2, s), a.getComponent(0), a.getComponent(1), a.getComponent(2), a.clone().set(s, t));
    P('v3.array', new THREE.Vector3().fromArray([s, t, 3, 4], 1), a.toArray());
  }
  P('v3.zero', new THREE.Vector3().normalize(), new THREE.Vector3().angleTo(new THREE.Vector3(1, 0, 0)), new THREE.Vector3(1, 2, 3).projectOnVector(new THREE.Vector3()), new THREE.Vector3().clampLength(1, 2));

  // --- Vector4
  for (let i = 0; i < 6; i++) {
    const a = V4(), b = V4(), s = r(-3, 3), t = rnd();
    const m = M4any(), q = Q();
    const lo = a.clone().min(b), hi = a.clone().max(b);
    const p = V4(-20, 20);
    P('v4.add', a.clone().add(b), a.clone().addScalar(s), new THREE.Vector4().addVectors(a, b), a.clone().addScaledVector(b, s));
    P('v4.sub', a.clone().sub(b), a.clone().subScalar(s), new THREE.Vector4().subVectors(a, b));
    P('v4.mul', a.clone().multiply(b), a.clone().multiplyScalar(s), a.clone().divide(b), a.clone().divideScalar(s));
    P('v4.applyMatrix4', a.clone().applyMatrix4(m), new THREE.Vector4().setFromMatrixPosition(m));
    P('v4.axisAngle', new THREE.Vector4().setAxisAngleFromQuaternion(q), new THREE.Vector4().setAxisAngleFromRotationMatrix(new THREE.Matrix4().makeRotationFromQuaternion(q)));
    P('v4.minmax', lo, hi, p.clone().clamp(lo, hi), p.clone().clampScalar(-2.5, 3.5), p.clone().clampLength(2, 9));
    P('v4.round', a.clone().floor(), a.clone().ceil(), a.clone().round(), a.clone().roundToZero(), a.clone().negate());
    P('v4.dot', a.dot(b), a.lengthSq(), a.length(), a.manhattanLength());
    P('v4.normalize', a.clone().normalize(), a.clone().setLength(s));
    P('v4.lerp', a.clone().lerp(b, t), new THREE.Vector4().lerpVectors(a, b, t), a.equals(b), a.equals(a.clone()));
    P('v4.misc', a.clone().setX(s), a.clone().setW(s), a.clone().setScalar(s), a.clone().setComponent(3, s), a.getComponent(2), a.getComponent(3), a.width, a.height, new THREE.Vector4().copy(new THREE.Vector3(s, t, 1)));
  }
  {
    // the axis and angle of rotations at and near the singular cases
    const cases = [
      new THREE.Matrix4(),
      new THREE.Matrix4().makeRotationX(Math.PI), new THREE.Matrix4().makeRotationY(Math.PI), new THREE.Matrix4().makeRotationZ(Math.PI),
      new THREE.Matrix4().makeRotationAxis(new THREE.Vector3(1, 1, 0).normalize(), Math.PI),
      new THREE.Matrix4().makeRotationAxis(new THREE.Vector3(0, 1, 1).normalize(), Math.PI),
      new THREE.Matrix4().makeRotationAxis(new THREE.Vector3(1, 0, 1).normalize(), Math.PI),
      new THREE.Matrix4().makeRotationAxis(new THREE.Vector3(1, 2, 3).normalize(), Math.PI),
      new THREE.Matrix4().makeRotationAxis(new THREE.Vector3(3, 2, 1).normalize(), 0.001),
      new THREE.Matrix4().makeRotationAxis(new THREE.Vector3(2, 3, 1).normalize(), 3.14),
    ];
    for (const m of cases) P('v4.axisAngle.case', new THREE.Vector4().setAxisAngleFromRotationMatrix(m));
    P('v4.axisAngle.identity', new THREE.Vector4().setAxisAngleFromQuaternion(new THREE.Quaternion()));
  }

  // --- Quaternion
  for (let i = 0; i < 12; i++) {
    const a = Q(), b = Q(), t = rnd(), s = r(-3, 3);
    const e = E(ORDERS[i % 6]), n = V3(-1, 1).normalize(), u = V3(-1, 1).normalize();
    const m = new THREE.Matrix4().makeRotationFromEuler(e);
    P('q.setFromEuler', e.order, new THREE.Quaternion().setFromEuler(e));
    P('q.setFromAxisAngle', new THREE.Quaternion().setFromAxisAngle(n, s));
    P('q.setFromRotationMatrix', new THREE.Quaternion().setFromRotationMatrix(m));
    P('q.setFromUnitVectors', new THREE.Quaternion().setFromUnitVectors(n, u), new THREE.Quaternion().setFromUnitVectors(n, n.clone().negate()), new THREE.Quaternion().setFromUnitVectors(n, n));
    P('q.angleTo', a.angleTo(b), a.clone().rotateTowards(b, 0.3), a.clone().rotateTowards(b, 10));
    P('q.invert', a.clone().invert(), a.clone().conjugate(), a.dot(b), a.lengthSq(), a.length());
    P('q.normalize', new THREE.Quaternion(s, t, 2, -1).normalize(), new THREE.Quaternion(0, 0, 0, 0).normalize());
    P('q.multiply', a.clone().multiply(b), a.clone().premultiply(b), new THREE.Quaternion().multiplyQuaternions(a, b));
    P('q.slerp', a.clone().slerp(b, t), a.clone().slerp(b, 0), a.clone().slerp(b, 1), new THREE.Quaternion().slerpQuaternions(a, b, t));
    // nearly the same rotation (the straight-line branch), and the far side (the negated one)
    const near = a.clone().multiply(new THREE.Quaternion().setFromAxisAngle(n, 0.01));
    const far = b.clone().set(-b.x, -b.y, -b.z, -b.w);
    P('q.slerp.near', a.clone().slerp(near, t), a.clone().slerp(far, t));
    const flat = [0, 0, 0, 0, 0, 0];
    THREE.Quaternion.slerpFlat(flat, 1, a.toArray(), 0, [9, ...b.toArray()], 1, t);
    P('q.slerpFlat', flat);
    THREE.Quaternion.slerpFlat(flat, 2, a.toArray(), 0, near.toArray(), 0, t);
    P('q.slerpFlat.near', flat);
    THREE.Quaternion.multiplyQuaternionsFlat(flat, 0, a.toArray(), 0, b.toArray(), 0);
    P('q.multiplyFlat', flat);
    P('q.misc', a.equals(b), a.equals(a.clone()), new THREE.Quaternion().identity(), new THREE.Quaternion().fromArray([1, s, t, 0.5, 2], 1), a.toArray());
  }
  {
    // rotations by nearly half a turn about each axis: the three branches without a positive trace
    const axes = [new THREE.Vector3(1, 0, 0), new THREE.Vector3(0, 1, 0), new THREE.Vector3(0, 0, 1), new THREE.Vector3(1, 1, 0).normalize(), new THREE.Vector3(0, 1, 1).normalize(), new THREE.Vector3(1, 0.2, 1).normalize()];
    for (const axis of axes) {
      for (const angle of [3.0, 3.14, Math.PI, 2.2]) {
        const m = new THREE.Matrix4().makeRotationAxis(axis, angle);
        P('q.setFromRotationMatrix.case', new THREE.Quaternion().setFromRotationMatrix(m));
      }
    }
  }

  // --- Euler
  for (let i = 0; i < 4; i++) {
    for (const order of ORDERS) {
      const e = E(order), q = Q();
      const m = new THREE.Matrix4().makeRotationFromEuler(e);
      P('e.matrix', order, m);
      P('e.setFromRotationMatrix', new THREE.Euler(0, 0, 0, order).setFromRotationMatrix(m), new THREE.Euler().setFromRotationMatrix(m, order));
      P('e.setFromQuaternion', new THREE.Euler(0, 0, 0, order).setFromQuaternion(q), new THREE.Euler().setFromQuaternion(q, order));
      P('e.roundtrip', new THREE.Quaternion().setFromEuler(new THREE.Euler().setFromQuaternion(q, order)));
      for (const other of ORDERS) P('e.reorder', e.clone().reorder(other));
      P('e.misc', new THREE.Euler().setFromVector3(new THREE.Vector3(e.x, e.z, e.y), order), new THREE.Euler(1, 2, 3, order).setFromVector3(new THREE.Vector3(e.x, e.z, e.y)), e.equals(e.clone()), e.clone().set(1, 2, 3), e.clone().set(1, 2, 3, 'ZYX'));
    }
  }
  for (const order of ORDERS) {
    // at the poles (gimbal lock): the middle angle a quarter turn
    for (const quarter of [Math.PI / 2, -Math.PI / 2]) {
      const a = r(-3, 3), c = r(-3, 3);
      const e = order[1] === 'X' ? new THREE.Euler(quarter, a, c, order) : order[1] === 'Y' ? new THREE.Euler(a, quarter, c, order) : new THREE.Euler(a, c, quarter, order);
      const m = new THREE.Matrix4().makeRotationFromEuler(e);
      P('e.pole', new THREE.Euler().setFromRotationMatrix(m, order), new THREE.Quaternion().setFromEuler(e));
    }
  }

  // --- Matrix3
  for (let i = 0; i < 6; i++) {
    const a = M3any(), b = M3any(), s = r(-3, 3), t = r(-3, 3), m4 = M4();
    P('m3.set', new THREE.Matrix3().set(1, 2, 3, 4, 5, 6, 7, 8, 9), new THREE.Matrix3(1, 2, 3, 4, 5, 6, 7, 8, 9), new THREE.Matrix3().copy(a).identity());
    P('m3.setFromMatrix4', new THREE.Matrix3().setFromMatrix4(m4), new THREE.Matrix3().getNormalMatrix(m4));
    P('m3.multiply', a.clone().multiply(b), a.clone().premultiply(b), new THREE.Matrix3().multiplyMatrices(a, b), a.clone().multiplyScalar(s));
    P('m3.determinant', a.determinant(), a.clone().invert(), a.clone().transpose());
    const x = new THREE.Vector3(), y = new THREE.Vector3(), z = new THREE.Vector3();
    a.extractBasis(x, y, z);
    const arr = [0, 0, 0, 0, 0, 0, 0, 0, 0];
    a.transposeIntoArray(arr);
    P('m3.extractBasis', x, y, z, arr);
    P('m3.uv', new THREE.Matrix3().setUvTransform(s, t, 2, 3, 0.7, 0.5, 0.25));
    P('m3.make', new THREE.Matrix3().makeTranslation(s, t), new THREE.Matrix3().makeTranslation(new THREE.Vector2(s, t)), new THREE.Matrix3().makeRotation(s), new THREE.Matrix3().makeScale(s, t));
    P('m3.deprecated', a.clone().scale(s, t), a.clone().rotate(s), a.clone().translate(s, t));
    P('m3.misc', a.equals(b), a.equals(a.clone()), new THREE.Matrix3().fromArray([9, ...a.toArray()], 1), a.toArray());
  }
  P('m3.singular', new THREE.Matrix3().set(1, 2, 3, 2, 4, 6, 0, 1, 5).invert(), new THREE.Matrix3().set(1, 2, 3, 2, 4, 6, 0, 1, 5).determinant());

  // --- Matrix4
  for (let i = 0; i < 8; i++) {
    const a = M4(), b = M4(), c = M4any(), s = r(-3, 3), t = r(-3, 3), u = r(-3, 3);
    const p = V3(), q = Q(), sc = V3(0.2, 3), n = V3(-1, 1).normalize(), e = E(ORDERS[i % 6]);
    P('m4.set', new THREE.Matrix4().set(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16), new THREE.Matrix4(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16), new THREE.Matrix4().copy(a).identity());
    P('m4.copyPosition', b.clone().copyPosition(a), new THREE.Matrix4().setFromMatrix3(M3any()));
    const x = new THREE.Vector3(), y = new THREE.Vector3(), z = new THREE.Vector3();
    a.extractBasis(x, y, z);
    P('m4.extractBasis', x, y, z, new THREE.Matrix4().makeBasis(x, y, z), new THREE.Matrix4().extractRotation(a));
    P('m4.makeRotationFromEuler', new THREE.Matrix4().makeRotationFromEuler(e), new THREE.Matrix4().makeRotationFromQuaternion(q));
    P('m4.lookAt', new THREE.Matrix4().lookAt(p, sc, n), a.clone().lookAt(p, sc, new THREE.Vector3(0, 1, 0)));
    P('m4.multiply', a.clone().multiply(b), a.clone().premultiply(b), new THREE.Matrix4().multiplyMatrices(a, c), a.clone().multiplyScalar(s));
    P('m4.determinant', a.determinant(), c.determinant(), a.determinantAffine(), c.determinantAffine());
    P('m4.transpose', c.clone().transpose(), a.clone().setPosition(p), a.clone().setPosition(s, t, u));
    P('m4.invert', a.clone().invert(), c.clone().invert());
    P('m4.scale', a.clone().scale(sc), a.getMaxScaleOnAxis(), c.getMaxScaleOnAxis());
    P('m4.makeTranslation', new THREE.Matrix4().makeTranslation(s, t, u), new THREE.Matrix4().makeTranslation(p));
    P('m4.makeRotation', new THREE.Matrix4().makeRotationX(s), new THREE.Matrix4().makeRotationY(t), new THREE.Matrix4().makeRotationZ(u), new THREE.Matrix4().makeRotationAxis(n, s));
    P('m4.makeScale', new THREE.Matrix4().makeScale(s, t, u), new THREE.Matrix4().makeShear(s, t, u, 0.5, -0.25, 2));
    const m = new THREE.Matrix4().compose(p, q, sc);
    const p2 = new THREE.Vector3(), q2 = new THREE.Quaternion(), s2 = new THREE.Vector3();
    m.decompose(p2, q2, s2);
    P('m4.compose', m, p2, q2, s2);
    // a mirrored one (negative determinant) and anything at all
    const mirrored = new THREE.Matrix4().compose(p, q, new THREE.Vector3(-sc.x, sc.y, sc.z));
    mirrored.decompose(p2, q2, s2);
    P('m4.decompose.mirrored', p2, q2, s2);
    c.decompose(p2, q2, s2);
    P('m4.decompose.any', p2, q2, s2);
    const near = 0.1 + rnd(), far = near + 10 + 1000 * rnd();
    P('m4.makePerspective', new THREE.Matrix4().makePerspective(s - 5, s + 4, t + 3, t - 2, near, far), new THREE.Matrix4().makeOrthographic(s - 5, s + 4, t + 3, t - 2, near, far));
    P('m4.misc', a.equals(b), a.equals(a.clone()), new THREE.Matrix4().fromArray([9, ...a.toArray()], 1), a.toArray());
  }
  {
    const flat = new THREE.Matrix4().makeScale(1, 0, 1);
    const p2 = new THREE.Vector3(), q2 = new THREE.Quaternion(0.5, 0.5, 0.5, 0.5), s2 = new THREE.Vector3(2, 2, 2);
    flat.setPosition(1, 2, 3).decompose(p2, q2, s2);
    const x = new THREE.Vector3(), y = new THREE.Vector3(), z = new THREE.Vector3();
    flat.extractBasis(x, y, z);
    P('m4.singular', flat.clone().invert(), p2, q2, s2, x, y, z, new THREE.Matrix4().extractRotation(flat), flat.determinant());
    // lookAt where there is nothing to look along, and straight up and down
    const o = new THREE.Vector3(1, 2, 3);
    P('m4.lookAt.same', new THREE.Matrix4().lookAt(o, o, new THREE.Vector3(0, 1, 0)));
    P('m4.lookAt.up', new THREE.Matrix4().lookAt(o, new THREE.Vector3(1, 7, 3), new THREE.Vector3(0, 1, 0)), new THREE.Matrix4().lookAt(o, new THREE.Vector3(1, -7, 3), new THREE.Vector3(0, 1, 0)));
    P('m4.lookAt.upZ', new THREE.Matrix4().lookAt(o, new THREE.Vector3(1, 2, 9), new THREE.Vector3(0, 0, 1)), new THREE.Matrix4().lookAt(o, new THREE.Vector3(1, 2, -9), new THREE.Vector3(0, 0, -1)));
  }

  // --- Color
  const hexes = [0x808890, 0xffffff, 0x000000, 0xc2d3e4, 0xfff2d0, 0x3a74c6, 0xff2a1a, 0x1aff6a, 0x141416, 0x0a0b0c, 0x010203, 0xfefdfc, 0x7f8081, 0x123456, 0xabcdef];
  for (const hex of hexes) {
    const c = new THREE.Color(hex);
    P('c.hex', c, c.getHex(), c.getHexString(), c.getStyle(), c.getHex(THREE.LinearSRGBColorSpace), c.getHexString(THREE.LinearSRGBColorSpace), c.getStyle(THREE.LinearSRGBColorSpace));
    P('c.setHex', new THREE.Color().setHex(hex), new THREE.Color().setHex(hex, THREE.LinearSRGBColorSpace), new THREE.Color().set(hex), new THREE.Color(1, 0, 0).setHex(hex + 0.75));
    P('c.getHSL', c.getHSL({}), c.getHSL({}, THREE.SRGBColorSpace), c.getRGB(new THREE.Color()), c.getRGB(new THREE.Color(), THREE.SRGBColorSpace));
  }
  const styles = ['#f00', '#fa3', '#FA3', '#808890', '#FFAA00', '#ffaa0', '#12', '#12345g', 'rgb(255,0,0)', 'rgb( 12 , 34 , 56 )', 'rgba(255,128,0,0.5)', 'rgba(255,128,0,.5)',
    'rgb(100%,50%,0%)', 'rgb(12.5%, .5%, 100%)', 'rgba(10%,20%,30%, 0.25 )', 'rgb(300,0,999)', 'rgb(150%,0%,7%)', 'hsl(120,50%,50%)', 'hsla(-30.5, 100%, 25%, .3)', 'hsl(+400,10%,90%)',
    'hsl(.5,0%,33%)', 'hsl(200, 100%, 100%)', 'hsl(20,120%,40%)', 'skyblue', 'SkyBlue', 'rebeccapurple', 'RED', 'darkslategrey', 'yellowgreen', 'aliceblue', 'nonsense', '',
    'rgb(1,2)', 'rgb(1,2,3,)', 'rgb(1.5,2,3)', 'rgb(1,2,3) and more', 'foo(1,2,3)', 'rgb 1,2,3', ' rgb(1,2,3)', 'hsl(120,50,50)', 'rgb(1,2,3', 'rgb(10,20,30,1.5.2)', 'rgb(10,20,30,2.)'];
  for (const style of styles) {
    const c = new THREE.Color(0.1, 0.2, 0.3).setStyle(style);
    P('c.setStyle', style, c, c.getHexString(), new THREE.Color(0.1, 0.2, 0.3).setStyle(style, THREE.LinearSRGBColorSpace), new THREE.Color(style), new THREE.Color(0.1, 0.2, 0.3).set(style));
  }
  for (let i = 0; i < 8; i++) {
    const a = C(), b = C(), s = rnd(), t = rnd(), h = r(-2, 2), m = M3any();
    P('c.setRGB', new THREE.Color().setRGB(a.r, a.g, a.b), new THREE.Color().setRGB(a.r, a.g, a.b, THREE.SRGBColorSpace), new THREE.Color().set(a.r, a.g, a.b), new THREE.Color().set(a), new THREE.Color().setScalar(s));
    P('c.setHSL', new THREE.Color().setHSL(h, s, t), new THREE.Color().setHSL(h, s, t, THREE.SRGBColorSpace), new THREE.Color().setHSL(h, 0, t), new THREE.Color().setHSL(h, 1.5, t), new THREE.Color().setHSL(h, s, t * 2 - 0.5));
    P('c.getHSL', a.getHSL({}), a.getHSL({}, THREE.SRGBColorSpace), a.getHex(), a.getHexString(), a.getStyle(), a.getStyle(THREE.LinearSRGBColorSpace));
    P('c.offsetHSL', a.clone().offsetHSL(h, s - 0.5, t - 0.5), a.clone().lerpHSL(b, t));
    P('c.convert', a.clone().convertSRGBToLinear(), a.clone().convertLinearToSRGB(), new THREE.Color().copySRGBToLinear(a), new THREE.Color().copyLinearToSRGB(a));
    P('c.add', a.clone().add(b), new THREE.Color().addColors(a, b), a.clone().addScalar(s), a.clone().sub(b), a.clone().multiply(b), a.clone().multiplyScalar(s * 3));
    P('c.lerp', a.clone().lerp(b, t), new THREE.Color().lerpColors(a, b, t), new THREE.Color().setFromVector3(new THREE.Vector3(s, t, h)), a.clone().applyMatrix3(m), a.equals(b), a.equals(a.clone()));
    P('c.over', a.clone().multiplyScalar(3).getHex(), a.clone().multiplyScalar(3).getStyle(), a.clone().addScalar(-2).getHex(), new THREE.Color().fromArray([9, s, t, h], 1), a.toArray());
  }
  P('c.grey', new THREE.Color(0.5, 0.5, 0.5).getHSL({}), new THREE.Color(0, 0, 0).getHSL({}), new THREE.Color(1, 1, 1).getHSL({}), new THREE.Color(1, 0, 0).getHSL({}), new THREE.Color(0, 1, 0).getHSL({}), new THREE.Color(0, 0, 1).getHSL({}), new THREE.Color(1, 0, 1).getHSL({}));
  P('c.default', new THREE.Color(), new THREE.Color(0), new THREE.Color(0xffffff), new THREE.Color('white'));

  // --- Box2
  for (let i = 0; i < 6; i++) {
    const a = B2(), b = B2(), p = V2(), q = V2(), s = r(0, 3);
    P('b2.set', a, b, new THREE.Box2().set(p, q), new THREE.Box2().setFromCenterAndSize(p, new THREE.Vector2(s, s + 1)), new THREE.Box2().setFromPoints([p, q, a.min]));
    P('b2.get', a.getCenter(new THREE.Vector2()), a.getSize(new THREE.Vector2()), a.isEmpty(), new THREE.Box2().isEmpty(), new THREE.Box2().getCenter(new THREE.Vector2(1, 1)), new THREE.Box2().getSize(new THREE.Vector2(1, 1)));
    P('b2.expand', a.clone().expandByPoint(p), a.clone().expandByVector(new THREE.Vector2(s, 1)), a.clone().expandByScalar(s));
    P('b2.contains', a.containsPoint(p), a.containsPoint(a.getCenter(new THREE.Vector2())), a.containsBox(b), a.clone().union(b).containsBox(b), a.intersectsBox(b), a.getParameter(p, new THREE.Vector2()));
    P('b2.clamp', a.clampPoint(p, new THREE.Vector2()), a.distanceToPoint(p), a.clone().intersect(b), a.clone().union(b), a.clone().translate(p), a.equals(b), a.equals(a.clone()));
  }
  P('b2.empty', new THREE.Box2().makeEmpty(), new THREE.Box2(new THREE.Vector2(0, 0), new THREE.Vector2(1, 1)).intersect(new THREE.Box2(new THREE.Vector2(2, 2), new THREE.Vector2(3, 3))));

  // --- Box3
  for (let i = 0; i < 8; i++) {
    const a = B3(), b = B3(), p = V3(), q = V3(), s = r(0, 3), m = M4();
    const sph = SPH(), pl = PL();
    P('b3.set', a, b, new THREE.Box3().set(p, q), new THREE.Box3().setFromCenterAndSize(p, new THREE.Vector3(s, s + 1, s + 2)), new THREE.Box3().setFromPoints([p, q, a.min]), new THREE.Box3().setFromArray([p.x, p.y, p.z, q.x, q.y, q.z, s, s, s]));
    P('b3.get', a.getCenter(new THREE.Vector3()), a.getSize(new THREE.Vector3()), a.isEmpty(), new THREE.Box3().isEmpty(), new THREE.Box3().getCenter(new THREE.Vector3(1, 1, 1)), new THREE.Box3().getSize(new THREE.Vector3(1, 1, 1)));
    P('b3.expand', a.clone().expandByPoint(p), a.clone().expandByVector(new THREE.Vector3(s, 1, 2)), a.clone().expandByScalar(s));
    P('b3.contains', a.containsPoint(p), a.containsPoint(a.getCenter(new THREE.Vector3())), a.containsBox(b), a.clone().union(b).containsBox(b), a.intersectsBox(b), a.getParameter(p, new THREE.Vector3()));
    P('b3.intersects', a.intersectsSphere(sph), a.intersectsPlane(pl), sph.intersectsBox(a), pl.intersectsBox(a));
    P('b3.clamp', a.clampPoint(p, new THREE.Vector3()), a.distanceToPoint(p), a.getBoundingSphere(new THREE.Sphere()), new THREE.Box3().getBoundingSphere(new THREE.Sphere(p.clone(), 3)));
    P('b3.combine', a.clone().intersect(b), a.clone().union(b), a.clone().applyMatrix4(m), a.clone().translate(p), a.equals(b), a.equals(a.clone()), new THREE.Box3().applyMatrix4(m));
  }
  {
    // triangles around a box: how many touch it, and which
    const box = new THREE.Box3(new THREE.Vector3(-2, -1, -3), new THREE.Vector3(2, 1.5, 3));
    let hits = 0;
    const which = [];
    for (let i = 0; i < 400; i++) {
      const c = V3(-5, 5), tri = new THREE.Triangle(c.clone().add(V3(-2.5, 2.5)), c.clone().add(V3(-2.5, 2.5)), c.clone().add(V3(-2.5, 2.5)));
      const hit = box.intersectsTriangle(tri);
      if (hit !== tri.intersectsBox(box)) throw new Error('intersectsBox');
      if (hit) hits++;
      if (i < 60) which.push(hit);
    }
    P('b3.intersectsTriangle', hits, which, new THREE.Box3().intersectsTriangle(new THREE.Triangle()));
  }

  // --- Sphere
  for (let i = 0; i < 8; i++) {
    const a = SPH(), b = SPH(), p = V3(), m = M4(), pl = PL();
    const pts = [V3(), V3(), V3(), V3(), V3()];
    P('s.setFromPoints', new THREE.Sphere().setFromPoints(pts), new THREE.Sphere().setFromPoints(pts, p), new THREE.Sphere().set(p, 2.5), new THREE.Sphere().copy(a));
    P('s.contains', a.containsPoint(p), a.containsPoint(a.center), a.distanceToPoint(p), a.intersectsSphere(b), a.intersectsPlane(pl), pl.intersectsSphere(a), pl.distanceToSphere(a));
    P('s.clamp', a.clampPoint(p, new THREE.Vector3()), a.clampPoint(a.center, new THREE.Vector3()), a.getBoundingBox(new THREE.Box3()), new THREE.Sphere().getBoundingBox(new THREE.Box3(p.clone(), p.clone())));
    P('s.apply', a.clone().applyMatrix4(m), a.clone().translate(p), a.clone().expandByPoint(p), a.clone().expandByPoint(a.center), new THREE.Sphere().expandByPoint(p));
    P('s.union', a.clone().union(b), a.clone().union(new THREE.Sphere()), new THREE.Sphere().union(b), a.clone().union(new THREE.Sphere(a.center, 9)), a.clone().union(new THREE.Sphere(a.center, 0.1)), a.equals(b), a.equals(a.clone()));
  }
  P('s.empty', new THREE.Sphere().isEmpty(), new THREE.Sphere(new THREE.Vector3(1, 2, 3), 0).isEmpty(), new THREE.Sphere(new THREE.Vector3(1, 2, 3), 2).makeEmpty());

  // --- Plane, Line3
  for (let i = 0; i < 8; i++) {
    const a = PL(), p = V3(), q = V3(), w = V3(), m = M4(), n = V3(-1, 1).normalize();
    const line = new THREE.Line3(p, q), other = new THREE.Line3(w, V3());
    P('p.set', new THREE.Plane().set(n, 2), new THREE.Plane().setComponents(1, 2, 3, 4), new THREE.Plane().setFromNormalAndCoplanarPoint(n, p), new THREE.Plane().setFromCoplanarPoints(p, q, w), new THREE.Plane().copy(a));
    P('p.normalize', new THREE.Plane(p.clone(), 3).normalize(), a.clone().negate(), a.distanceToPoint(p), a.projectPoint(p, new THREE.Vector3()), a.coplanarPoint(new THREE.Vector3()));
    P('p.intersectLine', a.intersectLine(line, new THREE.Vector3()), a.intersectLine(line, new THREE.Vector3(), false), a.intersectsLine(line));
    P('p.apply', a.clone().applyMatrix4(m), a.clone().applyMatrix4(m, new THREE.Matrix3().getNormalMatrix(m)), a.clone().translate(p), a.equals(a.clone()), a.equals(new THREE.Plane()));
    P('l.get', line.getCenter(new THREE.Vector3()), line.delta(new THREE.Vector3()), line.distanceSq(), line.distance(), line.at(0.3, new THREE.Vector3()));
    P('l.closest', line.closestPointToPointParameter(w, false), line.closestPointToPointParameter(w, true), line.closestPointToPoint(w, true, new THREE.Vector3()), line.closestPointToPoint(w, false, new THREE.Vector3()));
    {
      const c1 = new THREE.Vector3(), c2 = new THREE.Vector3();
      const d1 = line.distanceSqToLine3(other, c1, c2);
      const c1a = c1.clone(), c2a = c2.clone();
      const d2 = line.distanceSqToLine3(new THREE.Line3(w, w), c1, c2);
      const c1b = c1.clone(), c2b = c2.clone();
      const d3 = new THREE.Line3(p, p).distanceSqToLine3(other, c1, c2);
      P('l.distanceSqToLine3', d1, c1a, c2a, d2, c1b, c2b, d3, c1, c2);
    }
    P('l.apply', line.clone().applyMatrix4(m), line.equals(other), line.equals(line.clone()), new THREE.Line3(p, p).distanceSqToLine3(new THREE.Line3(w, w)), line.distanceSqToLine3(new THREE.Line3(p.clone().add(n), q.clone().add(n))));
  }
  {
    const plane = new THREE.Plane(new THREE.Vector3(0, 1, 0), -1);
    const inPlane = new THREE.Line3(new THREE.Vector3(1, 1, 1), new THREE.Vector3(4, 1, 2)), beside = new THREE.Line3(new THREE.Vector3(1, 2, 1), new THREE.Vector3(4, 2, 2));
    P('p.intersectLine.coplanar', plane.intersectLine(inPlane, new THREE.Vector3()), plane.intersectLine(beside, new THREE.Vector3()), plane.intersectsLine(inPlane), plane.intersectsLine(beside));
  }

  // --- Frustum
  for (let i = 0; i < 4; i++) {
    const proj = i % 2 === 0 ? new THREE.Matrix4().makePerspective(-0.08, 0.06, 0.05, -0.045, 0.1, 400) : new THREE.Matrix4().makeOrthographic(-20, 30, 12, -9, 0.5, 300);
    const view = new THREE.Matrix4().compose(V3(), Q(), new THREE.Vector3(1, 1, 1)).invert();
    const f = new THREE.Frustum().setFromProjectionMatrix(new THREE.Matrix4().multiplyMatrices(proj, view));
    P('f.planes', f.planes[0], f.planes[1], f.planes[2], f.planes[3], f.planes[4], f.planes[5]);
    const inSphere = [], inBox = [], inPoint = [];
    for (let k = 0; k < 60; k++) {
      const c = V3(-150, 150), rad = r(1, 60), d = V3(1, 40);
      inSphere.push(f.intersectsSphere(new THREE.Sphere(c, rad)));
      inBox.push(f.intersectsBox(new THREE.Box3(c.clone().sub(d), c.clone().add(d))));
      inPoint.push(f.containsPoint(c));
    }
    P('f.intersects', inSphere, inBox, inPoint);
    P('f.copy', new THREE.Frustum().copy(f).planes[3], f.clone().planes[5]);
  }

  // --- Ray
  for (let i = 0; i < 10; i++) {
    const a = RAY(), p = V3(), q = V3(), sph = SPH(), pl = PL(), box = B3(), m = M4(), t = r(0, 20);
    P('r.at', a.at(t, new THREE.Vector3()), a.clone().lookAt(p), a.clone().recast(t), new THREE.Ray().set(p, q), new THREE.Ray().copy(a));
    P('r.closest', a.closestPointToPoint(p, new THREE.Vector3()), a.distanceToPoint(p), a.distanceSqToPoint(p), a.closestPointToPoint(a.at(-3, new THREE.Vector3()), new THREE.Vector3()), a.distanceSqToPoint(a.at(-3, new THREE.Vector3())));
    P('r.sphere', a.intersectSphere(sph, new THREE.Vector3()), a.intersectsSphere(sph), new THREE.Ray(sph.center.clone().addScalar(0.1), a.direction).intersectSphere(sph, new THREE.Vector3()), a.intersectSphere(new THREE.Sphere(), new THREE.Vector3()), a.intersectsSphere(new THREE.Sphere()));
    P('r.plane', a.distanceToPlane(pl), a.intersectPlane(pl, new THREE.Vector3()), a.intersectsPlane(pl));
    P('r.box', a.intersectBox(box, new THREE.Vector3()), a.intersectsBox(box), new THREE.Ray(box.getCenter(new THREE.Vector3()), a.direction).intersectBox(box, new THREE.Vector3()));
    P('r.apply', a.clone().applyMatrix4(m), a.equals(a.clone()), a.equals(new THREE.Ray()));
  }
  {
    // rays along the axes (a division by zero in intersectBox) and in a plane
    const box = new THREE.Box3(new THREE.Vector3(-1, -2, -3), new THREE.Vector3(1, 2, 3));
    const dirs = [new THREE.Vector3(1, 0, 0), new THREE.Vector3(-1, 0, 0), new THREE.Vector3(0, 1, 0), new THREE.Vector3(0, -1, 0), new THREE.Vector3(0, 0, 1), new THREE.Vector3(0, 0, -1)];
    for (const d of dirs) {
      for (const o of [new THREE.Vector3(0.5, 0.5, 0.5), new THREE.Vector3(-5, 0.5, 0.5), new THREE.Vector3(0.5, 9, 0.5), new THREE.Vector3(0.5, 0.5, -7), new THREE.Vector3(1, 2, 20), new THREE.Vector3(4, 4, 4)]) {
        P('r.box.axis', new THREE.Ray(o, d).intersectBox(box, new THREE.Vector3()));
      }
    }
    const plane = new THREE.Plane(new THREE.Vector3(0, 1, 0), -1);
    P('r.plane.coplanar', new THREE.Ray(new THREE.Vector3(0, 1, 0), new THREE.Vector3(1, 0, 0)).distanceToPlane(plane), new THREE.Ray(new THREE.Vector3(0, 2, 0), new THREE.Vector3(1, 0, 0)).distanceToPlane(plane),
      new THREE.Ray(new THREE.Vector3(0, 2, 0), new THREE.Vector3(1, 0, 0)).intersectsPlane(plane), new THREE.Ray(new THREE.Vector3(0, 1, 0), new THREE.Vector3(1, 0, 0)).intersectsPlane(plane),
      new THREE.Ray(new THREE.Vector3(0, 2, 0), new THREE.Vector3(0, 1, 0)).intersectPlane(plane, new THREE.Vector3()), new THREE.Ray(new THREE.Vector3(0, 2, 0), new THREE.Vector3(0, -1, 0)).intersectPlane(plane, new THREE.Vector3()));
  }
  {
    // the distance to segments: every region of the algorithm is met with enough of them
    let sum = 0;
    for (let i = 0; i < 300; i++) {
      const a = RAY(), v0 = V3(), v1 = i % 7 === 0 ? v0.clone().addScaledVector(a.direction, r(1, 5)) : V3();
      const onRay = new THREE.Vector3(), onSegment = new THREE.Vector3();
      const d = a.distanceSqToSegment(v0, v1, onRay, onSegment);
      sum += d;
      if (i < 40) P('r.distanceSqToSegment', d, onRay, onSegment, a.distanceSqToSegment(v0, v1));
    }
    P('r.distanceSqToSegment.sum', sum);
  }
  {
    // triangles: hits from the front, from the back, and misses
    let hits = 0, culled = 0;
    const sum = new THREE.Vector3();
    for (let i = 0; i < 600; i++) {
      const a = RAY(), c = a.at(r(-4, 12), new THREE.Vector3());
      const ta = c.clone().add(V3(-3, 3)), tb = c.clone().add(V3(-3, 3)), tc = c.clone().add(V3(-3, 3));
      const target = new THREE.Vector3(7, 7, 7), target2 = new THREE.Vector3(7, 7, 7);
      const hit = a.intersectTriangle(ta, tb, tc, false, target), hit2 = a.intersectTriangle(ta, tb, tc, true, target2);
      if (hit) { hits++; sum.add(target); }
      if (hit2) culled++;
      if (i < 60) P('r.intersectTriangle', hit, hit2, target, target2);
    }
    P('r.intersectTriangle.sum', hits, culled, sum);
    // along each axis, both ways (the six cases of the largest direction component), through corners and edges
    const tri = [new THREE.Vector3(-1, -1, 0), new THREE.Vector3(2, -1, 0), new THREE.Vector3(-1, 2, 0)];
    for (const perm of [[0, 1, 2], [1, 2, 0], [2, 0, 1]]) {
      const pts = tri.map((v) => new THREE.Vector3().setComponent(perm[0], v.x).setComponent(perm[1], v.y).setComponent(perm[2], v.z));
      for (const sign of [1, -1]) {
        for (const at of [[0, 0], [-1, -1], [0.5, -1], [0.5, 0.5], [3, 3], [-1, 2]]) {
          const o = new THREE.Vector3().setComponent(perm[0], at[0]).setComponent(perm[1], at[1]).setComponent(perm[2], -5 * sign);
          const d = new THREE.Vector3().setComponent(perm[2], sign);
          P('r.intersectTriangle.axis', new THREE.Ray(o, d).intersectTriangle(pts[0], pts[1], pts[2], false, new THREE.Vector3()), new THREE.Ray(o, d).intersectTriangle(pts[0], pts[1], pts[2], true, new THREE.Vector3()),
            new THREE.Ray(o, d.clone().negate()).intersectTriangle(pts[0], pts[1], pts[2], false, new THREE.Vector3()));
        }
      }
    }
    P('r.intersectTriangle.degenerate', new THREE.Ray(new THREE.Vector3(0, 0, 5), new THREE.Vector3(0, 0, -1)).intersectTriangle(tri[0], tri[0], tri[1], false, new THREE.Vector3()),
      new THREE.Ray(new THREE.Vector3(0, 0, 5), new THREE.Vector3(0, 0, 0)).intersectTriangle(tri[0], tri[1], tri[2], false, new THREE.Vector3()),
      new THREE.Ray(new THREE.Vector3(0, 0, 0), new THREE.Vector3(1, 0, 0)).intersectTriangle(tri[0], tri[1], tri[2], false, new THREE.Vector3()));
  }

  // --- Triangle
  for (let i = 0; i < 10; i++) {
    const a = V3(), b = V3(), c = V3(), p = V3(), d = V3(-1, 1).normalize();
    const t = new THREE.Triangle(a, b, c);
    const inside = a.clone().multiplyScalar(0.2).addScaledVector(b, 0.5).addScaledVector(c, 0.3);
    P('t.normal', THREE.Triangle.getNormal(a, b, c, new THREE.Vector3()), t.getNormal(new THREE.Vector3()), t.getArea(), t.getMidpoint(new THREE.Vector3()), t.getPlane(new THREE.Plane()));
    P('t.barycoord', THREE.Triangle.getBarycoord(p, a, b, c, new THREE.Vector3()), t.getBarycoord(inside, new THREE.Vector3()), THREE.Triangle.containsPoint(p, a, b, c), t.containsPoint(inside));
    P('t.interpolation', THREE.Triangle.getInterpolation(inside, a, b, c, new THREE.Vector2(0, 0), new THREE.Vector2(1, 0), new THREE.Vector2(0, 1), new THREE.Vector2()),
      t.getInterpolation(p, new THREE.Vector3(1, 0, 0), new THREE.Vector3(0, 1, 0), new THREE.Vector3(0, 0, 1), new THREE.Vector3()));
    P('t.facing', THREE.Triangle.isFrontFacing(a, b, c, d), t.isFrontFacing(d.clone().negate()), t.equals(t.clone()), t.equals(new THREE.Triangle()));
    P('t.set', new THREE.Triangle().set(c, b, a), new THREE.Triangle().setFromPointsAndIndices([a, b, c, p], 3, 0, 2), new THREE.Triangle().copy(t));
  }
  {
    const a = new THREE.Vector3(1, 2, 3), b = new THREE.Vector3(2, 4, 6), c = new THREE.Vector3(4, 8, 12);
    P('t.degenerate', THREE.Triangle.getNormal(a, b, c, new THREE.Vector3(1, 1, 1)), THREE.Triangle.getBarycoord(a, a, b, c, new THREE.Vector3(1, 1, 1)), THREE.Triangle.containsPoint(a, a, b, c),
      THREE.Triangle.getInterpolation(a, a, b, c, new THREE.Vector2(0, 0), new THREE.Vector2(1, 0), new THREE.Vector2(0, 1), new THREE.Vector2(5, 5)), new THREE.Triangle(a, b, c).getArea());
    // the closest point: every region of the triangle is met with enough points
    const t = new THREE.Triangle(new THREE.Vector3(-2, 0, 1), new THREE.Vector3(3, 0.5, -1), new THREE.Vector3(0.5, 4, 0.5));
    const sum = new THREE.Vector3();
    for (let i = 0; i < 300; i++) {
      const p = V3(-8, 8), q = t.closestPointToPoint(p, new THREE.Vector3());
      sum.add(q);
      if (i < 40) P('t.closestPointToPoint', q);
    }
    P('t.closestPointToPoint.sum', sum, new THREE.Triangle(a, a, c).closestPointToPoint(new THREE.Vector3(0, 0, 0), new THREE.Vector3()), new THREE.Triangle(a, a, c).closestPointToPoint(new THREE.Vector3(3, 5, 9), new THREE.Vector3()));
  }

  // --- Spherical
  for (let i = 0; i < 6; i++) {
    const p = V3();
    const s = new THREE.Spherical().setFromVector3(p);
    P('sph.set', s, new THREE.Vector3().setFromSpherical(s), new THREE.Spherical(2, i < 3 ? 0 : 4, 1).makeSafe(), new THREE.Spherical().setFromCartesianCoords(0, 0, 0), new THREE.Spherical().set(1, 2, 3), new THREE.Spherical().copy(s), s.clone());
  }
}

// ---------------------------------------------------------------------------

const sections = { math: mathSection };
const wanted = process.argv.slice(2).length ? process.argv.slice(2) : Object.keys(sections);
for (const name of wanted) {
  if (!sections[name]) throw new Error('no such section: ' + name);
  lines.push('# ' + name);
  sections[name]();
}
process.stdout.write(lines.join('\n') + '\n');
