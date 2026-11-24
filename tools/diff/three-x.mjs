// SPDX-License-Identifier: AGPL-3.0-only
// three.js itself, asked what the engine is asked in tools/dev/three_x_dump.cpp: the two
// print the same lines, tools/diff/compare.py compares them.
//
//   node tools/diff/three-x.mjs [math] [scene] [geom] > /tmp/a.txt      (no section named: all)
//   FS_JOBS=1 tools/t.sh three_x_dump > /dev/null && build/three_x_dump [math] [scene] [geom] > /tmp/b.txt
//   tools/diff/compare.py /tmp/a.txt /tmp/b.txt
//
// three.js is taken from the original's node_modules (THREE_DIR names another place).
// Keep the two files alike, section by section and line by line: the random numbers
// are drawn in the same order on both sides.

const THREE_DIR = process.env.THREE_DIR || '/home/user/driving-simulator/node_modules/three';
// (three.js warns about texts that are no colours and the like: they are asked on purpose)
console.warn = () => {};
console.error = () => {};
const THREE = await import(THREE_DIR + '/build/three.module.js');
const { mergeGeometries, mergeVertices } = await import(THREE_DIR + '/examples/jsm/utils/BufferGeometryUtils.js');
const { Earcut } = await import(THREE_DIR + '/src/extras/Earcut.js');

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
// what the scene and the geometry sections share
// ---------------------------------------------------------------------------

function sums(array) {
  let s = 0, q = 0;
  for (let i = 0; i < array.length; i++) {
    s += array[i];
    q += array[i] * array[i];
  }
  return [s, q];
}
/** an attribute: its kind of array, count, itemSize, normalized, the sum and the sum of squares, the first twelve numbers */
function A(label, attribute) {
  if (!attribute) {
    P(label, 'none');
    return;
  }
  const a = attribute.array;
  P(label, a.constructor.name, attribute.count, attribute.itemSize, attribute.normalized, sums(a), Array.from(a.slice(0, 12)));
}
/** a geometry: its attributes in their order, the index, the groups, the draw range and the bounds it has */
function G(label, g) {
  const names = Object.keys(g.attributes);
  P(label, names.join(',') || 'none', g.index ? g.index.count : -1, g.groups.length, g.drawRange.start, g.drawRange.count === Infinity ? -1 : g.drawRange.count);
  for (const name of names) A(label + '.' + name, g.attributes[name]);
  A(label + '.index', g.index);
  for (const group of g.groups) P(label + '.group', group.start, group.count, group.materialIndex);
  P(label + '.bounds', g.boundingBox, g.boundingSphere);
}
/** a wavy sheet of nx × ny squares, with an index */
function grid(nx, ny, w, h) {
  const pos = [], uv = [], idx = [];
  for (let j = 0; j <= ny; j++) {
    for (let i = 0; i <= nx; i++) {
      pos.push((i / nx - 0.5) * w, (j / ny - 0.5) * h, 0.3 * Math.sin(i * 1.7 + j * 0.9));
      uv.push(i / nx, j / ny);
    }
  }
  for (let j = 0; j < ny; j++) {
    for (let i = 0; i < nx; i++) {
      const a = j * (nx + 1) + i, b = a + 1, c = a + nx + 1, d = c + 1;
      idx.push(a, b, d, a, d, c);
    }
  }
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.BufferAttribute(new Float32Array(pos), 3));
  g.setAttribute('uv', new THREE.BufferAttribute(new Float32Array(uv), 2));
  g.setIndex(idx);
  return g;
}
/** n triangles anywhere, without an index */
function soup(n, spread = 4) {
  const pos = [];
  for (let i = 0; i < n; i++) {
    const c = V3(-spread, spread);
    for (let k = 0; k < 3; k++) {
      const p = c.clone().add(V3(-1, 1));
      pos.push(p.x, p.y, p.z);
    }
  }
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.BufferAttribute(new Float32Array(pos), 3));
  return g;
}

// ---------------------------------------------------------------------------
// the scene graph
// ---------------------------------------------------------------------------

function sceneSection() {
  seed(77001);

  // --- the angles and the quaternion of an object are one
  {
    const o = new THREE.Object3D();
    P('rot.start', o.rotation, o.quaternion);
    for (let i = 0; i < 6; i++) {
      const order = ORDERS[i];
      const x = r(-3, 3), y = r(-3, 3), z = r(-3, 3);
      o.rotation.order = order;
      P('rot.order', o.rotation, o.quaternion);
      o.rotation.set(x, y, z);
      P('rot.set', o.rotation, o.quaternion);
      o.rotation.y = z;
      P('rot.y', o.rotation, o.quaternion);
      o.quaternion.copy(Q());
      P('rot.fromQuaternion', o.rotation, o.quaternion);
      o.rotation.y = x;
      P('rot.y.after', o.rotation, o.quaternion);
      o.rotation.x += 0.25;
      o.rotation.z -= 0.5;
      P('rot.plus', o.rotation, o.quaternion, o.rotation.x, o.rotation.y, o.rotation.z);
      o.quaternion.setFromAxisAngle(V3(-1, 1).normalize(), y);
      o.rotation.z = 1;
      P('rot.z', o.rotation, o.quaternion);
      o.rotateY(0.4);
      o.rotateOnWorldAxis(new THREE.Vector3(1, 0, 0), 0.3);
      P('rot.rotateY', o.rotation, o.quaternion);
      o.rotation.copy(new THREE.Euler(z, x, y, ORDERS[(i + 2) % 6]));
      P('rot.copy', o.rotation, o.quaternion);
      o.quaternion.multiply(Q());
      o.rotation.order = order;
      P('rot.order.after', o.rotation, o.quaternion);
      o.rotation.setFromQuaternion(Q());
      P('rot.setFromQuaternion', o.rotation, o.quaternion);
      o.rotation.setFromRotationMatrix(new THREE.Matrix4().makeRotationFromQuaternion(Q()));
      P('rot.setFromRotationMatrix', o.rotation, o.quaternion);
      o.rotation.set(y, z, x, ORDERS[(i + 3) % 6]);
      o.updateMatrix();
      P('rot.set.order', o.rotation, o.quaternion, o.matrix);
      o.quaternion.slerp(Q(), 0.3);
      o.rotation.x *= 2;
      P('rot.times', o.rotation, o.quaternion);
    }
    const other = new THREE.Object3D();
    other.rotation.copy(o.rotation);
    P('rot.copy.rotation', other.rotation, other.quaternion);
  }

  // --- a small tree
  const mat = new THREE.MeshLambertMaterial({ color: 0x808890 });
  {
    const root = new THREE.Scene(), g1 = new THREE.Group(), g2 = new THREE.Group(), m1 = new THREE.Mesh(grid(3, 2, 4, 2), mat), m2 = new THREE.Object3D();
    root.name = 'root';
    g1.name = 'g1';
    g2.name = 'g2';
    m1.name = 'm1';
    m2.name = 'm2';
    root.add(g1);
    g1.add(g2);
    g2.add(m1);
    g1.add(m2);
    const place = (o, s) => {
      o.position.copy(V3());
      o.quaternion.copy(Q());
      o.scale.setScalar(s);
    };
    place(g1, 1.5);
    place(g2, 0.8);
    place(m1, 1);
    place(m2, 2);
    root.updateMatrixWorld();
    P('tree.world', g1.matrix, g1.matrixWorld, g2.matrixWorld, m1.matrixWorld, m2.matrixWorld);
    // a change below: only what is asked for is brought up to date
    g2.rotation.y = 0.7;
    g2.position.x += 1;
    m1.updateWorldMatrix(true, false);
    P('tree.updateWorldMatrix', g2.matrixWorld, m1.matrixWorld, m2.matrixWorld);
    g1.rotateOnAxis(V3(-1, 1).normalize(), 0.5);
    g1.rotateOnWorldAxis(V3(-1, 1).normalize(), -0.3);
    g1.rotateX(0.1).rotateY(0.2).rotateZ(0.3);
    g1.translateOnAxis(V3(-1, 1).normalize(), 2).translateX(1).translateY(2).translateZ(3);
    g1.updateWorldMatrix(false, true);
    P('tree.rotate', g1.position, g1.quaternion, g1.matrixWorld, m1.matrixWorld, m2.matrixWorld);
    P('tree.getWorld', m1.getWorldPosition(new THREE.Vector3()), m1.getWorldQuaternion(new THREE.Quaternion()), m1.getWorldScale(new THREE.Vector3()), m1.getWorldDirection(new THREE.Vector3()));
    const v = V3();
    P('tree.localToWorld', m1.localToWorld(v.clone()), m1.worldToLocal(v.clone()));
    // a matrix of its own, a world matrix of its own
    m2.matrixAutoUpdate = false;
    m2.position.set(100, 100, 100);
    m2.matrix.makeRotationX(0.5).setPosition(1, 2, 3);
    root.updateMatrixWorld(true);
    P('tree.ownMatrix', m2.matrix, m2.matrixWorld);
    m2.matrixAutoUpdate = true;
    m2.position.set(1, 2, 3);
    g2.matrixWorldAutoUpdate = false;
    g2.position.y += 5;
    root.updateMatrixWorld(true);
    P('tree.ownWorldMatrix', g2.matrix, g2.matrixWorld, m1.matrixWorld, m2.matrixWorld);
    g2.matrixWorldAutoUpdate = true;
    root.updateMatrixWorld(true);
    // applyMatrix4, applyQuaternion
    m2.applyMatrix4(M4());
    P('tree.applyMatrix4', m2.position, m2.quaternion, m2.scale, m2.matrix);
    m2.applyQuaternion(Q());
    P('tree.applyQuaternion', m2.quaternion, m2.rotation);
    // lookAt: an object looks along +z, a camera and a light along -z
    const target = V3();
    m1.lookAt(target);
    P('tree.lookAt', m1.quaternion, m1.rotation);
    m1.lookAt(target.x, target.y + 3, target.z);
    P('tree.lookAt.xyz', m1.quaternion);
    const cam = new THREE.PerspectiveCamera(), sun = new THREE.DirectionalLight(0xffffff, 1);
    cam.name = 'cam';
    sun.name = 'sun';
    g2.add(cam, sun);
    cam.position.copy(V3());
    sun.position.copy(V3());
    cam.lookAt(target);
    sun.lookAt(target);
    P('tree.lookAt.camera', cam.quaternion, sun.quaternion, cam.getWorldDirection(new THREE.Vector3()), sun.getWorldDirection(new THREE.Vector3()));
    cam.up.set(0, 0, 1);
    cam.lookAt(target);
    P('tree.lookAt.up', cam.quaternion, cam.matrixWorldInverse);
    // attach: to another parent, staying where it is in the world
    root.updateMatrixWorld(true);
    P('tree.beforeAttach', m1.matrixWorld, m2.matrixWorld);
    root.attach(m1);
    P('tree.attach', m1.position, m1.quaternion, m1.scale, m1.matrixWorld, m1.parent === root, g2.children.length, root.children.length);
    g2.attach(m2);
    P('tree.attach.back', m2.position, m2.quaternion, m2.scale, m2.matrixWorld, m2.parent === g2);
    // add, remove, clear
    const names = (o) => o.children.map((c) => c.name).join(',') || 'none';
    P('tree.children', names(root), names(g1), names(g2));
    g1.add(m1);   // (it leaves the root)
    P('tree.add', names(root), names(g1), m1.parent === g1);
    g1.add(g1);   // (itself: nothing happens)
    g1.remove(m2);   // (not a child of g1: nothing happens)
    g1.remove(m1);
    P('tree.remove', names(g1), m1.parent === null);
    g2.add(m1);
    m1.removeFromParent();
    P('tree.removeFromParent', names(g2), m1.parent === null);
    g2.add(m1);
    const order = [];
    root.traverse((o) => order.push(o.name));
    P('tree.traverse', order.join(','));
    g2.visible = false;
    const seen = [];
    root.traverseVisible((o) => seen.push(o.name));
    P('tree.traverseVisible', seen.join(','));
    P('tree.getObjectByName', root.getObjectByName('m1') === m1, root.getObjectByName('nothing') === undefined, g2.getObjectByName('g1') === undefined, m1.getObjectByName('m1') === m1);
    g2.clear();
    P('tree.clear', names(g2), m1.parent === null, cam.parent === null);
  }

  // --- cameras
  for (let i = 0; i < 4; i++) {
    const fov = r(20, 100), aspect = r(0.5, 2.5), near = r(0.05, 2), far = r(100, 3000);
    const cam = new THREE.PerspectiveCamera(fov, aspect, near, far);
    P('cam.perspective', cam.projectionMatrix, cam.projectionMatrixInverse);
    cam.zoom = r(0.5, 3);
    cam.updateProjectionMatrix();
    P('cam.zoom', cam.projectionMatrix);
    const ox = r(0, 900), oy = r(0, 500);
    cam.setViewOffset(1920, 1080, ox, oy, 640, 360);
    P('cam.viewOffset', cam.aspect, cam.projectionMatrix, cam.projectionMatrixInverse);
    cam.clearViewOffset();
    P('cam.clearViewOffset', cam.aspect, cam.projectionMatrix);
    cam.position.copy(V3());
    cam.quaternion.copy(Q());
    cam.updateMatrixWorld();
    P('cam.world', cam.matrixWorld, cam.matrixWorldInverse, cam.getWorldDirection(new THREE.Vector3()));
    const p = V3(-30, 30);
    const ndc = p.clone().project(cam);
    P('cam.project', ndc, ndc.clone().unproject(cam));
    // scaled: matrixWorldInverse leaves the scale out
    cam.scale.set(2, 2, 2);
    cam.updateMatrixWorld();
    P('cam.scaled', cam.matrixWorld, cam.matrixWorldInverse);
    cam.scale.set(1, 1, 1);
    // below a parent
    const rig = new THREE.Group();
    rig.position.copy(V3());
    rig.quaternion.copy(Q());
    rig.add(cam);
    cam.updateWorldMatrix(true, false);
    P('cam.rig', cam.matrixWorld, cam.matrixWorldInverse);
    cam.lookAt(p);
    P('cam.rig.lookAt', cam.quaternion);
    // orthographic
    const left = r(-50, -5), right = r(5, 50), top = r(5, 50), bottom = r(-50, -5);
    const ortho = new THREE.OrthographicCamera(left, right, top, bottom, near, far);
    P('cam.orthographic', ortho.projectionMatrix, ortho.projectionMatrixInverse);
    ortho.zoom = r(0.5, 3);
    ortho.updateProjectionMatrix();
    P('cam.orthographic.zoom', ortho.projectionMatrix);
    ortho.setViewOffset(800, 600, ox / 4, oy / 4, 320, 240);
    P('cam.orthographic.viewOffset', ortho.projectionMatrix, ortho.projectionMatrixInverse);
    ortho.clearViewOffset();
    P('cam.orthographic.clearViewOffset', ortho.projectionMatrix);
    ortho.position.copy(V3());
    ortho.quaternion.copy(Q());
    ortho.updateMatrixWorld();
    const ndc2 = p.clone().project(ortho);
    P('cam.orthographic.project', ndc2, ndc2.clone().unproject(ortho));
    // rays through a point of the picture
    const ray = new THREE.Raycaster();
    const at = V2(-1, 1);
    ray.setFromCamera(at, cam);
    P('cam.ray', ray.ray);
    ray.setFromCamera(at, ortho);
    P('cam.ray.orthographic', ray.ray);
  }
  P('cam.default', new THREE.PerspectiveCamera().projectionMatrix, new THREE.OrthographicCamera(-1, 1, 1, -1).projectionMatrix, new THREE.Camera().projectionMatrix);

  // --- lights as three.js makes them
  {
    const dir = new THREE.DirectionalLight(0xfff2d0, 2.5);
    P('light.directional', dir.color, dir.intensity, dir.position, dir.matrix, dir.target.position, dir.shadow.camera.projectionMatrix, dir.shadow.camera.near, dir.shadow.camera.far,
      dir.shadow.mapSize, dir.shadow.bias, dir.shadow.normalBias, dir.shadow.radius, dir.shadow.intensity, dir.shadow.autoUpdate, dir.shadow.needsUpdate, dir.castShadow);
    const hemi = new THREE.HemisphereLight(0xc2d3e4, 0x6a5d4a, 1.2);
    P('light.hemisphere', hemi.color, hemi.groundColor, hemi.intensity, hemi.position, hemi.matrix);
    const amb = new THREE.AmbientLight(0x404040);
    P('light.ambient', amb.color, amb.intensity, amb.position);
    const point = new THREE.PointLight(0xffb000, 3, 25, 1.5), point2 = new THREE.PointLight(0xffffff);
    P('light.point', point.color, point.intensity, point.distance, point.decay, point.position, point.shadow.camera.projectionMatrix, point2.intensity, point2.distance, point2.decay);
    const spot = new THREE.SpotLight(0xfff0c8, 40, 60, 0.5, 0.3, 1.2), spot2 = new THREE.SpotLight(0xffffff);
    P('light.spot', spot.color, spot.intensity, spot.distance, spot.angle, spot.penumbra, spot.decay, spot.position, spot.matrix, spot.target.position, spot.shadow.camera.projectionMatrix,
      spot2.intensity, spot2.distance, spot2.angle, spot2.penumbra, spot2.decay);
    // before a shadow map is drawn: the shadow camera at the light, looking at its target, and the matrix into the map
    dir.position.copy(V3());
    dir.target.position.copy(V3());
    dir.updateMatrixWorld();
    dir.target.updateMatrixWorld();
    dir.shadow.updateMatrices(dir);
    P('light.shadow.directional', dir.shadow.camera.position, dir.shadow.camera.quaternion, dir.shadow.camera.matrixWorldInverse, dir.shadow.matrix, dir.shadow.getFrustum().planes[0], dir.shadow.getFrustum().planes[5]);
    spot.position.copy(V3());
    spot.target.position.copy(V3());
    spot.shadow.mapSize.set(1024, 512);
    spot.updateMatrixWorld();
    spot.target.updateMatrixWorld();
    spot.shadow.updateMatrices(spot);
    P('light.shadow.spot', spot.shadow.camera.fov, spot.shadow.camera.aspect, spot.shadow.camera.far, spot.shadow.camera.projectionMatrix, spot.shadow.matrix);
    spot2.updateMatrixWorld();
    spot2.shadow.updateMatrices(spot2);
    P('light.shadow.spot.endless', spot2.shadow.camera.fov, spot2.shadow.camera.aspect, spot2.shadow.camera.far, spot2.shadow.matrix);
  }

  // --- levels of detail
  {
    const lod = new THREE.LOD(), near = new THREE.Group(), mid = new THREE.Group(), far = new THREE.Group(), cam = new THREE.PerspectiveCamera();
    near.name = 'near';
    mid.name = 'mid';
    far.name = 'far';
    lod.addLevel(far, 200, 0.1);
    lod.addLevel(near);
    lod.addLevel(mid, -50, 0.2);   // (a distance below zero counts as its amount)
    P('lod.levels', lod.levels.map((l) => l.object.name).join(','), lod.levels.map((l) => l.distance), lod.levels.map((l) => l.hysteresis), lod.children.length, lod.getCurrentLevel());
    lod.position.set(10, 0, -5);
    lod.updateMatrixWorld();
    for (const d of [0, 30, 45, 55, 45, 41, 39, 150, 185, 199, 201, 185, 181, 179, 60, 20, 500, 0]) {
      cam.position.set(10 + d, 0, -5);
      cam.updateMatrixWorld();
      lod.update(cam);
      P('lod.update', d, lod.getCurrentLevel(), near.visible, mid.visible, far.visible);
    }
    cam.zoom = 4;
    cam.position.set(10 + 300, 0, -5);
    cam.updateMatrixWorld();
    lod.update(cam);
    P('lod.zoom', lod.getCurrentLevel(), near.visible, mid.visible, far.visible);
    // one level: there is nothing to choose
    const single = new THREE.LOD(), only = new THREE.Group();
    only.visible = false;
    single.addLevel(only);
    single.update(cam);
    P('lod.single', single.getCurrentLevel(), only.visible);
  }

  // --- attributes
  {
    const kinds = [['Uint8', Uint8Array], ['Int8', Int8Array], ['Uint16', Uint16Array], ['Int16', Int16Array], ['Uint32', Uint32Array], ['Int32', Int32Array], ['Float32', Float32Array]];
    for (const [name, Type] of kinds) {
      for (const normalized of [false, true]) {
        const a = new THREE.BufferAttribute(new Type(8), 4, normalized);
        const x = rnd(), y = r(-1, 1), z = r(-2, 2), w = r(0, 300);
        a.setXYZW(0, x, y, z, w);
        a.setX(1, y);
        a.setY(1, x);
        a.setZ(1, w);
        a.setW(1, z);
        P('attr.' + name, normalized, Array.from(a.array), a.getX(0), a.getY(0), a.getZ(0), a.getW(0), a.getX(1), a.getY(1), a.getZ(1), a.getW(1), a.count);
        a.setXY(0, w, z);
        a.setXYZ(1, x, x, y);
        P('attr.' + name + '.xy', Array.from(a.array));
      }
    }
    // clone, and the transforms
    const pos = new THREE.BufferAttribute(new Float32Array(12), 3);
    for (let i = 0; i < 4; i++) {
      const p = V3();
      pos.setXYZ(i, p.x, p.y, p.z);
    }
    const m = M4(), m3 = M3any();
    const c = pos.clone();
    c.applyMatrix4(m);
    A('attr.applyMatrix4', c);
    A('attr.clone', pos);
    A('attr.applyNormalMatrix', pos.clone().applyNormalMatrix(new THREE.Matrix3().getNormalMatrix(m)));
    A('attr.transformDirection', pos.clone().transformDirection(m));
    A('attr.applyMatrix3', pos.clone().applyMatrix3(m3));
    const uv = new THREE.BufferAttribute(new Float32Array([0, 0, 1, 0, 1, 1, 0.25, 0.75]), 2);
    A('attr.applyMatrix3.uv', uv.applyMatrix3(m3));
    const inst = new THREE.InstancedBufferAttribute(new Float32Array(6), 3);
    const ic = inst.clone();
    P('attr.instanced', ic.isInstancedBufferAttribute === true, ic.meshPerAttribute, ic.count);
  }

  // --- geometries
  {
    const g = grid(4, 3, 6, 4);
    G('geo.grid', g);
    g.computeVertexNormals();
    g.computeBoundingBox();
    g.computeBoundingSphere();
    G('geo.normals', g);
    // 16 bits where they fit
    A('geo.index16', new THREE.BufferGeometry().setIndex([0, 1, 65534]).index);
    A('geo.index32', new THREE.BufferGeometry().setIndex([0, 65535, 2]).index);
    // transforms: the bounds follow, the normals turn with the inverse transpose
    const m = new THREE.Matrix4().compose(V3(), Q(), new THREE.Vector3(1.5, 0.5, 2));
    G('geo.applyMatrix4', g.clone().applyMatrix4(m));
    G('geo.rotate', g.clone().rotateX(0.3).rotateY(-0.7).rotateZ(1.1));
    G('geo.translate', g.clone().translate(1, 2, 3).scale(2, 3, 4));
    G('geo.applyQuaternion', g.clone().applyQuaternion(Q()));
    G('geo.center', g.clone().translate(5, 6, 7).center());
    // without an index
    const n = g.toNonIndexed();
    G('geo.toNonIndexed', n);
    n.computeVertexNormals();
    G('geo.toNonIndexed.normals', n);
    P('geo.toNonIndexed.again', n.toNonIndexed() === n);
    // groups, a draw range, colours as bytes: all of it is copied
    const c = g.clone();
    const col = new THREE.BufferAttribute(new Uint8Array(c.attributes.position.count * 4), 4, true);
    for (let i = 0; i < col.count; i++) {
      const cr = rnd(), cg = rnd(), cb = rnd();
      col.setXYZW(i, cr, cg, cb, 1);
    }
    c.setAttribute('color', col);
    c.addGroup(0, 30, 0);
    c.addGroup(30, 42, 1);
    c.setDrawRange(6, 36);
    c.name = 'copy me';
    const c2 = c.clone();
    G('geo.clone', c2);
    P('geo.clone.name', c2.name, c2.index !== c.index, c2.attributes.position !== c.attributes.position);
    G('geo.clone.toNonIndexed', c.toNonIndexed());
    c2.clearGroups();
    c2.deleteAttribute('uv');
    c2.setAttribute('uv', c.attributes.uv);
    c2.setAttribute('position', c.attributes.position);
    G('geo.attributes', c2);
    // triangles without an index
    const s = soup(6);
    s.computeVertexNormals();
    s.computeBoundingSphere();
    G('geo.soup', s);
    // from points
    const pts = [];
    for (let i = 0; i < 4; i++) pts.push(V3());
    const fp = new THREE.BufferGeometry().setFromPoints(pts);
    G('geo.setFromPoints', fp);
    const two = [];
    for (let i = 0; i < 2; i++) two.push(V3());
    fp.setFromPoints(two);
    G('geo.setFromPoints.again', fp);
    const six = [];
    for (let i = 0; i < 6; i++) six.push(V3());
    fp.setFromPoints(six);
    G('geo.setFromPoints.more', fp);
    // nothing in it
    const e = new THREE.BufferGeometry();
    e.computeBoundingBox();
    e.computeBoundingSphere();
    e.computeVertexNormals();
    G('geo.empty', e);
  }

  // --- instances, the box of a tree, what a frustum holds
  {
    const im = new THREE.InstancedMesh(grid(2, 2, 1, 1), mat, 5);
    A('inst.matrix', im.instanceMatrix);
    A('inst.color', im.instanceColor);
    for (let i = 0; i < 5; i++) im.setMatrixAt(i, M4());
    const back = new THREE.Matrix4();
    im.getMatrixAt(3, back);
    P('inst.getMatrixAt', back);
    im.setColorAt(2, new THREE.Color(0x3a74c6));
    A('inst.instanceColor', im.instanceColor);
    const cc = new THREE.Color();
    im.getColorAt(2, cc);
    P('inst.getColorAt', cc, im.getColorAt(0, new THREE.Color(0, 0, 0)), im.instanceColor.meshPerAttribute, im.instanceMatrix.meshPerAttribute);
    im.computeBoundingSphere();
    P('inst.boundingSphere', im.boundingSphere);
    im.count = 3;
    im.computeBoundingSphere();
    P('inst.boundingSphere.count', im.boundingSphere, im.instanceMatrix.count);
    // the box of a tree: of the geometries' boxes, or of every vertex
    const root = new THREE.Group(), mesh = new THREE.Mesh(grid(3, 3, 2, 2), mat), child = new THREE.Mesh(soup(4), mat);
    root.add(mesh, im);
    mesh.add(child);
    for (const o of [root, mesh, child, im]) {
      o.position.copy(V3());
      o.quaternion.copy(Q());
      o.scale.setScalar(r(0.5, 2));
    }
    root.updateMatrixWorld(true);
    P('box.setFromObject', new THREE.Box3().setFromObject(root), new THREE.Box3().setFromObject(root, true), new THREE.Box3().setFromObject(child), new THREE.Box3().setFromObject(new THREE.Group()));
    // what a frustum holds
    const cam = new THREE.PerspectiveCamera(60, 1.5, 0.1, 100);
    cam.position.copy(V3());
    cam.lookAt(0, 0, 0);
    cam.updateMatrixWorld();
    const f = new THREE.Frustum().setFromProjectionMatrix(new THREE.Matrix4().multiplyMatrices(cam.projectionMatrix, cam.matrixWorldInverse));
    const inside = [];
    for (let i = 0; i < 30; i++) {
      mesh.position.copy(V3(-30, 30));
      root.updateMatrixWorld(true);
      inside.push(f.intersectsObject(mesh), f.intersectsObject(im), f.intersectsObject(child));
    }
    P('frustum.intersectsObject', inside);
    const sprite = new THREE.Sprite(new THREE.SpriteMaterial());
    sprite.center.set(0.5, 0);
    sprite.scale.set(3, 2, 1);
    const spriteInside = [];
    for (let i = 0; i < 20; i++) {
      sprite.position.copy(V3(-30, 30));
      sprite.updateMatrixWorld();
      spriteInside.push(f.intersectsSprite(sprite));
    }
    P('frustum.intersectsSprite', spriteInside);
    P('box.sprite', new THREE.Box3().setFromObject(sprite), new THREE.Box3().setFromObject(sprite, true));
  }

  // --- raycasts
  {
    const hitsOf = (label, hits) => {
      P(label, hits.length);
      for (const h of hits.slice(0, 4)) P(label + '.hit', h.distance, h.point, h.face.a, h.face.b, h.face.c, h.face.normal, h.instanceId === undefined ? -1 : h.instanceId, h.object.name);
    };
    /** a ray at a point of the object (given in its own frame), from somewhere near */
    const rayAt = (object, x, y, z, near = 0, far = Infinity) => {
      const at = object.localToWorld(new THREE.Vector3(x, y, z));
      const from = at.clone().add(V3(-6, 6));
      return new THREE.Raycaster(from, at.clone().sub(from).normalize(), near, far);
    };
    const mesh = new THREE.Mesh(grid(6, 5, 8, 6), new THREE.MeshBasicMaterial());
    mesh.name = 'grid';
    mesh.position.copy(V3());
    mesh.quaternion.copy(Q());
    mesh.scale.set(1.2, 0.8, 1.5);
    mesh.updateMatrixWorld();
    const sides = [THREE.FrontSide, THREE.BackSide, THREE.DoubleSide];
    let total = 0;
    for (let i = 0; i < 45; i++) {
      const x = r(-5, 5), y = r(-4, 4);
      const ray = rayAt(mesh, x, y, 0);
      mesh.material.side = sides[i % 3];
      const hits = ray.intersectObject(mesh);
      total += hits.length;
      if (i < 12) hitsOf('ray.grid', hits);
    }
    P('ray.grid.total', total);
    // near and far
    mesh.material.side = THREE.DoubleSide;
    {
      const at = mesh.localToWorld(new THREE.Vector3(0.5, 0.5, 0)), from = at.clone().add(new THREE.Vector3(3, 4, 5)), dir = at.clone().sub(from).normalize();
      for (const [near, far] of [[0, Infinity], [0, 5], [8, 20], [6.5, 7.5], [0, 6.9], [7.3, Infinity]]) hitsOf('ray.nearFar', new THREE.Raycaster(from, dir, near, far).intersectObject(mesh));
    }
    // a bounding box, when the geometry has one, is asked first
    mesh.geometry.computeBoundingBox();
    total = 0;
    for (let i = 0; i < 20; i++) {
      const x = r(-7, 7), y = r(-6, 6);
      const hits = rayAt(mesh, x, y, 0).intersectObject(mesh);
      total += hits.length;
      if (i < 4) hitsOf('ray.box', hits);
    }
    P('ray.box.total', total);
    // the draw range
    mesh.geometry.setDrawRange(30, 60);
    total = 0;
    for (let i = 0; i < 30; i++) {
      const x = r(-4, 4), y = r(-3, 3);
      const hits = rayAt(mesh, x, y, 0).intersectObject(mesh);
      total += hits.length;
      if (i < 4) hitsOf('ray.drawRange', hits);
    }
    P('ray.drawRange.total', total);
    mesh.geometry.setDrawRange(0, Infinity);
    // groups, each with its own material: the front of one, the back of the other
    mesh.geometry.addGroup(0, 90, 0);
    mesh.geometry.addGroup(90, 90, 1);
    mesh.material = [new THREE.MeshBasicMaterial({ side: THREE.FrontSide }), new THREE.MeshBasicMaterial({ side: THREE.BackSide })];
    total = 0;
    for (let i = 0; i < 40; i++) {
      const x = r(-4, 4), y = r(-3, 3);
      const hits = rayAt(mesh, x, y, 0).intersectObject(mesh);
      total += hits.length;
      if (i < 6) hitsOf('ray.groups', hits);
    }
    P('ray.groups.total', total);
    mesh.geometry.setDrawRange(60, 60);
    total = 0;
    for (let i = 0; i < 30; i++) {
      const x = r(-4, 4), y = r(-3, 3);
      total += rayAt(mesh, x, y, 0).intersectObject(mesh).length;
    }
    P('ray.groups.drawRange.total', total);
    mesh.geometry.setDrawRange(0, Infinity);
    // without an index
    const soupMesh = new THREE.Mesh(soup(60, 5), new THREE.MeshBasicMaterial({ side: THREE.DoubleSide }));
    soupMesh.name = 'soup';
    soupMesh.position.copy(V3());
    soupMesh.quaternion.copy(Q());
    soupMesh.updateMatrixWorld();
    total = 0;
    for (let i = 0; i < 40; i++) {
      const x = r(-5, 5), y = r(-5, 5), z = r(-5, 5);
      const hits = rayAt(soupMesh, x, y, z).intersectObject(soupMesh);
      total += hits.length;
      if (i < 6) hitsOf('ray.soup', hits);
    }
    P('ray.soup.total', total);
    // instances
    const im = new THREE.InstancedMesh(grid(2, 2, 2, 2), new THREE.MeshBasicMaterial({ side: THREE.DoubleSide }), 6);
    im.name = 'instances';
    const centres = [];
    for (let i = 0; i < 6; i++) {
      const p = V3(-8, 8), q = Q();
      centres.push(p);
      im.setMatrixAt(i, new THREE.Matrix4().compose(p, q, new THREE.Vector3(1, 1, 1)));
    }
    im.position.copy(V3());
    im.updateMatrixWorld();
    total = 0;
    for (let i = 0; i < 30; i++) {
      const c = centres[i % 6];
      const hits = rayAt(im, c.x, c.y, c.z).intersectObject(im);
      total += hits.length;
      if (i < 8) hitsOf('ray.instances', hits);
    }
    P('ray.instances.total', total);
    // a tree: all of it, or only the object asked; a layer the raycaster does not see
    const group = new THREE.Group();
    group.name = 'group';
    group.add(mesh, soupMesh, im);
    group.updateMatrixWorld(true);
    for (let i = 0; i < 6; i++) {
      const x = r(-3, 3), y = r(-3, 3);
      const ray = rayAt(mesh, x, y, 0);
      hitsOf('ray.tree', ray.intersectObject(group));
      hitsOf('ray.tree.notRecursive', ray.intersectObject(group, false));
      hitsOf('ray.tree.objects', ray.intersectObjects([im, mesh, soupMesh], false));
      mesh.layers.set(2);
      hitsOf('ray.tree.layer', ray.intersectObject(group));
      mesh.layers.set(0);
    }
    // levels of detail: the level for the distance is asked (and, when all below is asked, every level once more)
    const lod = new THREE.LOD(), fine = new THREE.Mesh(grid(4, 4, 4, 4), new THREE.MeshBasicMaterial({ side: THREE.DoubleSide })), coarse = new THREE.Mesh(grid(1, 1, 4, 4), new THREE.MeshBasicMaterial({ side: THREE.DoubleSide }));
    fine.name = 'fine';
    coarse.name = 'coarse';
    lod.addLevel(fine, 0);
    lod.addLevel(coarse, 50);
    lod.updateMatrixWorld(true);
    for (const z of [10, 80]) {
      const ray = new THREE.Raycaster(new THREE.Vector3(0.3, 0.4, z), new THREE.Vector3(0, 0, -1));
      hitsOf('ray.lod', ray.intersectObject(lod, false));
      hitsOf('ray.lod.recursive', ray.intersectObject(lod, true));
    }
  }

  // --- materials and textures as three.js makes them
  {
    const BLEND = ['None', 'Normal', 'Additive', 'Subtractive', 'Multiply'];
    const FILTER = { 1003: 'Nearest', 1004: 'NearestMipmapNearest', 1005: 'NearestMipmapLinear', 1006: 'Linear', 1007: 'LinearMipmapNearest', 1008: 'LinearMipmapLinear' };
    const WRAP = { 1000: 'Repeat', 1001: 'ClampToEdge', 1002: 'MirroredRepeat' };
    const info = (label, m) => P(label, m.color ? m.color : 'none', m.opacity, m.transparent, m.side, m.depthWrite, m.depthTest, m.fog === true, m.toneMapped, m.vertexColors, m.alphaTest, BLEND[m.blending],
      m.polygonOffset, m.polygonOffsetFactor, m.polygonOffsetUnits, m.colorWrite, m.visible, m.name);
    const basic = new THREE.MeshBasicMaterial(), lambert = new THREE.MeshLambertMaterial(), phong = new THREE.MeshPhongMaterial(), line = new THREE.LineBasicMaterial(),
      sprite = new THREE.SpriteMaterial(), points = new THREE.PointsMaterial(), shader = new THREE.ShaderMaterial();
    info('mat.basic', basic);
    info('mat.lambert', lambert);
    P('mat.lambert.more', lambert.emissive, lambert.emissiveIntensity, lambert.flatShading);
    info('mat.phong', phong);
    P('mat.phong.more', phong.emissive, phong.emissiveIntensity, phong.flatShading, phong.specular, phong.shininess);
    info('mat.line', line);
    info('mat.sprite', sprite);
    P('mat.sprite.more', sprite.rotation, sprite.sizeAttenuation);
    info('mat.points', points);
    P('mat.points.more', points.size, points.sizeAttenuation);
    info('mat.shader', shader);
    const full = new THREE.MeshPhongMaterial({ color: 0x3a74c6, emissive: 0x1aff6a, emissiveIntensity: 0.5, specular: 0xfff2d0, shininess: 80, transparent: true, opacity: 0.4, side: THREE.DoubleSide,
      depthWrite: false, depthTest: false, fog: false, toneMapped: false, vertexColors: true, alphaTest: 0.3, blending: THREE.AdditiveBlending, polygonOffset: true, polygonOffsetFactor: -2,
      polygonOffsetUnits: -4, colorWrite: false, flatShading: true, name: 'full' });
    info('mat.params', full);
    P('mat.params.more', full.emissive, full.emissiveIntensity, full.flatShading, full.specular, full.shininess);
    const copy = full.clone();
    full.color.setHex(0);
    info('mat.clone', copy);
    P('mat.clone.more', copy.emissive, copy.emissiveIntensity, copy.flatShading, copy.specular, copy.shininess);
    info('mat.sprite.params', new THREE.SpriteMaterial({ transparent: false, color: 0xff2a1a, blending: THREE.MultiplyBlending, side: THREE.BackSide }));
    info('mat.shader.params', new THREE.ShaderMaterial({ fog: true, transparent: true }));
    info('mat.sprite.clone', sprite.clone());
    info('mat.shader.clone', shader.clone());
    const sm = new THREE.ShaderMaterial({ vertexShader: 'v', fragmentShader: 'f', uniforms: { a: { value: 1.5 }, c: { value: new THREE.Color(0x808890) }, v: { value: new THREE.Vector3(1, 2, 3) } }, defines: { SNOW: '' } });
    const sc = sm.clone();
    sc.uniforms.a.value = 2;
    sc.uniforms.c.value.setHex(0xff0000);
    sc.uniforms.v.value.x = 9;
    P('mat.shader.uniforms', sm.uniforms.a.value, sm.uniforms.c.value, sm.uniforms.v.value, sc.uniforms.a.value, sc.uniforms.c.value, sc.uniforms.v.value, sc.vertexShader, sc.fragmentShader, Object.keys(sc.defines).join(','));
    const tex = (label, t) => P(label, FILTER[t.magFilter], FILTER[t.minFilter], t.generateMipmaps, t.flipY, WRAP[t.wrapS], WRAP[t.wrapT], t.anisotropy, t.repeat, t.offset, t.version);
    tex('tex.plain', new THREE.Texture());
    tex('tex.canvas', new THREE.CanvasTexture(null));
    const dt = new THREE.DataTexture(new Uint8Array(16), 2, 2);
    tex('tex.data', dt);
    P('tex.data.size', dt.image.width, dt.image.height, dt.image.data.length);
    const rt = new THREE.WebGLRenderTarget(320, 200);
    tex('tex.target', rt.texture);
    P('tex.target.size', rt.width, rt.height, rt.texture.image.width, rt.texture.image.height, rt.depthBuffer, rt.samples, rt.texture.isRenderTargetTexture);
    rt.setSize(64, 48);
    P('tex.target.setSize', rt.width, rt.height, rt.texture.image.width, rt.texture.image.height, new THREE.WebGLRenderTarget(8, 8, { depthBuffer: false }).depthBuffer);
  }
}

// ---------------------------------------------------------------------------
// geometries, curves, shapes
// ---------------------------------------------------------------------------

function geomSection() {
  seed(424242);
  /** a geometry with its bounding sphere */
  const GS = (label, g) => {
    g.computeBoundingSphere();
    G(label, g);
  };
  /** numbers in a row: how many, their sum, the sum of each times its place, and the first of them */
  const row = (label, a, first = 24) => {
    let s = 0, w = 0;
    for (let i = 0; i < a.length; i++) {
      s += a[i];
      w += a[i] * (i + 1);
    }
    P(label, a.length, s, w, a.slice(0, first));
  };
  const flat2 = (pts) => pts.flatMap((p) => [p.x, p.y]);
  const flat3 = (pts) => pts.flatMap((p) => [p.x, p.y, p.z]);
  const PI = Math.PI;

  // --- the solids
  GS('box', new THREE.BoxGeometry());
  GS('box.2', new THREE.BoxGeometry(2, 3, 4));
  GS('box.3', new THREE.BoxGeometry(1.5, 0.25, 3, 2, 3, 4));
  GS('box.4', new THREE.BoxGeometry(0.13, 0.26, 0.1));
  GS('plane', new THREE.PlaneGeometry());
  GS('plane.2', new THREE.PlaneGeometry(4, 3));
  GS('plane.3', new THREE.PlaneGeometry(10, 5, 4, 3));
  GS('cylinder', new THREE.CylinderGeometry());
  GS('cylinder.2', new THREE.CylinderGeometry(0.5, 0.5, 2, 8));
  GS('cylinder.3', new THREE.CylinderGeometry(0.3, 0.6, 1.5, 12, 3, true));
  GS('cylinder.4', new THREE.CylinderGeometry(0, 1, 2, 6, 2));
  GS('cylinder.5', new THREE.CylinderGeometry(1, 0, 2, 6, 2));
  GS('cylinder.6', new THREE.CylinderGeometry(0.5, 0.5, 1, 8, 1, false, 0.3, 2.2));
  GS('cylinder.7', new THREE.CylinderGeometry(0.045, 0.05, 2.75, 8, 1, true));
  GS('cone', new THREE.ConeGeometry());
  GS('cone.2', new THREE.ConeGeometry(0.5, 1.2, 10));
  GS('cone.3', new THREE.ConeGeometry(1, 2, 8, 3, true, 0.5, 4));
  GS('sphere', new THREE.SphereGeometry());
  GS('sphere.2', new THREE.SphereGeometry(0.5, 8, 6));
  GS('sphere.3', new THREE.SphereGeometry(1, 20, 8, PI * 1.5 - 1.1, 2.2, 1.12, 0.62));
  GS('sphere.4', new THREE.SphereGeometry(1, 12, 6, 0, PI * 2, 0, PI / 2));
  GS('sphere.5', new THREE.SphereGeometry(2, 5, 4, 1, 3, 0.5, 2));
  GS('sphere.6', new THREE.SphereGeometry(1, 6, 10, 0.3, 1, 0.02, 1.75));
  GS('sphere.7', new THREE.SphereGeometry(1, 2, 1));
  GS('torus', new THREE.TorusGeometry());
  GS('torus.2', new THREE.TorusGeometry(0.3, 0.02, 6, 24));
  GS('torus.3', new THREE.TorusGeometry(1, 0.4, 8, 16, PI));
  GS('torus.4', new THREE.TorusGeometry(1, 0.3, 6, 12, PI * 2, 0.5, 2));
  GS('circle', new THREE.CircleGeometry());
  GS('circle.2', new THREE.CircleGeometry(0.5, 12));
  GS('circle.3', new THREE.CircleGeometry(1, 16, 0.3, 2.5));
  GS('circle.4', new THREE.CircleGeometry(1, 2));
  GS('ring', new THREE.RingGeometry());
  GS('ring.2', new THREE.RingGeometry(0.2, 0.5, 12, 2));
  GS('ring.3', new THREE.RingGeometry(1, 2, 8, 3, 0.4, 3));
  GS('ring.4', new THREE.RingGeometry(1, 2, 2, 0));
  const profile = [new THREE.Vector2(0.2, -0.11), new THREE.Vector2(0.27, -0.12), new THREE.Vector2(0.32, -0.09), new THREE.Vector2(0.34, 0), new THREE.Vector2(0.32, 0.09),
    new THREE.Vector2(0.27, 0.12), new THREE.Vector2(0.2, 0.11)];
  GS('lathe', new THREE.LatheGeometry([new THREE.Vector2(0, -0.5), new THREE.Vector2(0.5, 0), new THREE.Vector2(0, 0.5)]));
  GS('lathe.2', new THREE.LatheGeometry(profile, 22));
  GS('lathe.3', new THREE.LatheGeometry(profile, 10, 0.5, 4));
  GS('lathe.4', new THREE.LatheGeometry([new THREE.Vector2(0.5, 0), new THREE.Vector2(0.25, 1)], 5, 0, 9));
  GS('icosahedron', new THREE.IcosahedronGeometry());
  GS('icosahedron.2', new THREE.IcosahedronGeometry(0.5, 1));
  GS('icosahedron.3', new THREE.IcosahedronGeometry(1, 2));
  GS('icosahedron.4', new THREE.IcosahedronGeometry(2, 3));
  const tetra = [1, 1, 1, -1, -1, 1, -1, 1, -1, 1, -1, -1], tetraFaces = [2, 1, 0, 0, 3, 2, 1, 3, 0, 2, 3, 1];
  GS('polyhedron', new THREE.PolyhedronGeometry(tetra, tetraFaces));
  GS('polyhedron.2', new THREE.PolyhedronGeometry(tetra, tetraFaces, 1.5, 2));
  // … as the game makes them: turned, moved, merged, without an index, with normals of their own
  GS('game.cylinder', new THREE.CylinderGeometry(0.03, 0.03, 0.02, 12).rotateX(PI / 2).translate(0, 1.2, -0.09));
  GS('game.sphere', new THREE.SphereGeometry(0.235, 16, 4, 0, PI * 2, 0, PI / 2).scale(1, 0.32, 1).translate(0, 0.87, 0));
  GS('game.lathe', new THREE.LatheGeometry(profile, 22).rotateZ(-PI / 2));
  GS('game.box', new THREE.BoxGeometry(0.13, 0.26, 0.1).translate(0, 1.4, -0.09).toNonIndexed());
  {
    const g = new THREE.BoxGeometry(1, 2, 3, 2, 1, 1).toNonIndexed();
    g.deleteAttribute('normal');
    g.computeVertexNormals();
    GS('box.flat', g);
    const smooth = new THREE.SphereGeometry(1, 8, 6);
    smooth.deleteAttribute('normal');
    smooth.computeVertexNormals();
    GS('sphere.computeVertexNormals', smooth);
  }

  // --- curves
  const pts3 = [];
  for (let i = 0; i < 6; i++) pts3.push(V3(-3, 3));
  const curves3 = [
    ['line3', new THREE.LineCurve3(pts3[0], pts3[1])],
    ['quadratic3', new THREE.QuadraticBezierCurve3(pts3[0], pts3[1], pts3[2])],
    ['cubic3', new THREE.CubicBezierCurve3(pts3[0], pts3[1], pts3[2], pts3[3])],
    ['catmull', new THREE.CatmullRomCurve3(pts3)],
    ['catmull.closed', new THREE.CatmullRomCurve3(pts3, true)],
    ['catmull.chordal', new THREE.CatmullRomCurve3(pts3, false, 'chordal')],
    ['catmull.uniform', new THREE.CatmullRomCurve3(pts3, true, 'catmullrom', 0.7)],
    ['catmull.two', new THREE.CatmullRomCurve3([pts3[4], pts3[5]], false, 'centripetal')],
  ];
  for (const [name, c] of curves3) {
    P('curve.' + name, c.getPoint(0), c.getPoint(0.37), c.getPoint(1), c.getPointAt(0.37), c.getTangent(0.37), c.getTangentAt(0.37), c.getTangent(0), c.getTangentAt(1), c.getLength());
    row('curve.' + name + '.points', flat3(c.getPoints(7)));
    row('curve.' + name + '.spaced', flat3(c.getSpacedPoints(7)));
    row('curve.' + name + '.lengths', c.getLengths(10), 11);
    P('curve.' + name + '.mapping', c.getUtoTmapping(0.25), c.getUtoTmapping(0, c.getLength() * 0.6), c.getUtoTmapping(0), c.getUtoTmapping(1));
    for (const closed of [false, true]) {
      const f = c.computeFrenetFrames(9, closed);
      row('curve.' + name + '.frenet', flat3(f.tangents).concat(flat3(f.normals), flat3(f.binormals)), 9);
    }
  }
  row('curve.default', flat3(new THREE.CatmullRomCurve3(pts3).getPoints()).concat(flat3(new THREE.CatmullRomCurve3(pts3).getSpacedPoints())), 6);
  const pts2 = [];
  for (let i = 0; i < 5; i++) pts2.push(V2(-3, 3));
  const curves2 = [
    ['line', new THREE.LineCurve(pts2[0], pts2[1])],
    ['quadratic', new THREE.QuadraticBezierCurve(pts2[0], pts2[1], pts2[2])],
    ['cubic', new THREE.CubicBezierCurve(pts2[0], pts2[1], pts2[2], pts2[3])],
    ['spline', new THREE.SplineCurve(pts2)],
    ['ellipse', new THREE.EllipseCurve()],
    ['ellipse.arc', new THREE.EllipseCurve(1, 2, 3, 1.5, 0.4, 2.9, false, 0.6)],
    ['ellipse.clockwise', new THREE.EllipseCurve(1, 2, 3, 1.5, 0.4, 2.9, true)],
    ['ellipse.back', new THREE.EllipseCurve(0, 0, 2, 2, 3, 1, false)],
    ['ellipse.full', new THREE.EllipseCurve(0, 0, 2, 2, 1, 1 + PI * 2, true)],
    ['ellipse.none', new THREE.EllipseCurve(0, 0, 2, 2, 1, 1, false)],
  ];
  for (const [name, c] of curves2) {
    P('curve.' + name, c.getPoint(0), c.getPoint(0.37), c.getPoint(1), c.getPointAt(0.37), c.getTangent(0.37), c.getTangentAt(0.37), c.getLength());
    row('curve.' + name + '.points', flat2(c.getPoints(7)));
    row('curve.' + name + '.spaced', flat2(c.getSpacedPoints(7)));
  }

  // --- paths and shapes
  const outline = (s) => {
    const e = s.extractPoints(6);
    row('shape.points', flat2(e.shape), 16);
    for (const h of e.holes) row('shape.hole', flat2(h), 16);
  };
  const path = new THREE.Path();
  path.moveTo(0, 0).lineTo(4, 0).quadraticCurveTo(5, 0, 5, 1).lineTo(5, 3).bezierCurveTo(5, 4, 4, 5, 3, 4.5).absarc(2, 4, 1, 0.4, 2.6, false).lineTo(0, 3).absellipse(0, 2, 0.5, 1, PI / 2, -PI / 2, true, 0.2);
  P('path', path.curves.length, path.currentPoint, path.getLength(), path.getCurveLengths(), path.getPoint(0), path.getPoint(0.5), path.getPoint(1), path.getPointAt(0.3), path.getTangent(0.3));
  row('path.points', flat2(path.getPoints()), 16);
  row('path.points.4', flat2(path.getPoints(4)), 16);
  row('path.spaced', flat2(path.getSpacedPoints()), 16);
  row('path.spaced.9', flat2(path.getSpacedPoints(9)), 16);
  path.autoClose = true;
  row('path.autoClose', flat2(path.getPoints(3)).concat(flat2(path.getSpacedPoints(5))), 8);
  path.autoClose = false;
  path.closePath();
  P('path.closePath', path.curves.length, path.getLength(), path.currentPoint);
  path.closePath();
  P('path.closePath.again', path.curves.length);
  const rel = new THREE.Path().moveTo(1, 1).arc(1, 0, 1, PI, 0, true).ellipse(1, 0, 1, 0.5, PI, 0, false, 0).splineThru([new THREE.Vector2(5, 2), new THREE.Vector2(6, 0), new THREE.Vector2(7, 1)]);
  P('path.arc', rel.curves.length, rel.currentPoint, rel.getLength());
  row('path.arc.points', flat2(rel.getPoints(5)), 16);
  const fromPoints = new THREE.Path(pts2);
  P('path.fromPoints', fromPoints.curves.length, fromPoints.currentPoint, fromPoints.getLength());
  const copy = fromPoints.clone();
  copy.lineTo(9, 9);
  P('path.clone', copy.curves.length, fromPoints.curves.length, copy.curves[0] !== fromPoints.curves[0], copy.getLength());

  const triangle = new THREE.Shape([new THREE.Vector2(-0.3, 0), new THREE.Vector2(0, 0.52), new THREE.Vector2(0.3, 0)]);
  triangle.holes.push(new THREE.Path([new THREE.Vector2(-0.15, 0.05), new THREE.Vector2(0, 0.31), new THREE.Vector2(0.15, 0.05)]));
  const rect = new THREE.Shape([new THREE.Vector2(0, 0), new THREE.Vector2(2, 0), new THREE.Vector2(2, 1), new THREE.Vector2(0, 1)]);
  const rectCw = new THREE.Shape([new THREE.Vector2(0, 0), new THREE.Vector2(0, 1), new THREE.Vector2(2, 1), new THREE.Vector2(2, 0), new THREE.Vector2(0, 0)]);
  const rounded = new THREE.Shape();
  rounded.moveTo(0.2, 0).lineTo(1.8, 0).quadraticCurveTo(2, 0, 2, 0.2).lineTo(2, 0.8).quadraticCurveTo(2, 1, 1.8, 1).lineTo(0.2, 1).quadraticCurveTo(0, 1, 0, 0.8).lineTo(0, 0.2).quadraticCurveTo(0, 0, 0.2, 0);
  const disc = new THREE.Shape();
  disc.absarc(0, 0, 1, 0, PI * 2, false);
  const hole1 = new THREE.Path();
  hole1.absarc(0.3, 0.2, 0.25, 0, PI * 2, true);
  const hole2 = new THREE.Path();
  hole2.moveTo(-0.6, -0.2).lineTo(-0.2, -0.2).lineTo(-0.2, 0.2).lineTo(-0.6, 0.2).closePath();
  disc.holes.push(hole1, hole2);
  const blob = new THREE.Shape();
  blob.moveTo(0, 0).bezierCurveTo(1, -1, 3, -1, 4, 0).bezierCurveTo(5, 1, 5, 3, 4, 4).lineTo(2, 3).lineTo(0, 4).bezierCurveTo(-1, 3, -1, 1, 0, 0);
  // a long profile: so many points that the triangulation sorts them along a curve
  const long = new THREE.Shape();
  long.moveTo(0, 0);
  for (let i = 1; i <= 60; i++) long.lineTo(i * 0.5, Math.sin(i * 0.7) * 0.4 + (i % 2) * 0.3);
  long.lineTo(30, 3);
  for (let i = 60; i >= 0; i--) long.lineTo(i * 0.5, 3 + Math.cos(i * 0.4) * 0.5 + (i % 3) * 0.2);
  const longHole = new THREE.Path();
  longHole.absellipse(15, 1.6, 6, 0.3, 0, PI * 2, true, 0);
  long.holes.push(longHole);
  const shapes = [['triangle', triangle], ['rect', rect], ['rectCw', rectCw], ['rounded', rounded], ['disc', disc], ['blob', blob], ['long', long]];
  for (const [name, s] of shapes) {
    P('shape.' + name, s.curves.length, s.holes.length, s.getLength(), THREE.ShapeUtils.area(s.getPoints()), THREE.ShapeUtils.isClockWise(s.getPoints()));
    outline(s);
    GS('shape.' + name + '.geometry', new THREE.ShapeGeometry(s));
    GS('shape.' + name + '.geometry.4', new THREE.ShapeGeometry(s, 4));
    GS('shape.' + name + '.extrude', new THREE.ExtrudeGeometry(s, { depth: 0.6, bevelEnabled: false }));
    GS('shape.' + name + '.extrude.bevel', new THREE.ExtrudeGeometry(s));
  }
  GS('shape.several', new THREE.ShapeGeometry([triangle, rect, disc], 5));
  GS('shape.rotated', new THREE.ShapeGeometry(rounded).rotateX(PI / 2));
  GS('extrude.several', new THREE.ExtrudeGeometry([triangle, rect], { depth: 2, bevelEnabled: false, steps: 3, curveSegments: 3 }));
  GS('extrude.steps', new THREE.ExtrudeGeometry(rounded, { depth: 1.5, steps: 4, bevelEnabled: false, curveSegments: 5 }));
  GS('extrude.bevel', new THREE.ExtrudeGeometry(disc, { depth: 0.4, bevelEnabled: true, bevelThickness: 0.05, bevelSize: 0.04, bevelOffset: -0.01, bevelSegments: 2, curveSegments: 8 }));
  GS('extrude.bevel.default', new THREE.ExtrudeGeometry(rect, { bevelThickness: 0.3 }));
  GS('extrude.path', new THREE.ExtrudeGeometry(rect, { steps: 12, extrudePath: new THREE.CatmullRomCurve3(pts3) }));
  GS('extrude.path.closed', new THREE.ExtrudeGeometry(triangle, { steps: 16, extrudePath: new THREE.CatmullRomCurve3(pts3, true), curveSegments: 4 }));
  GS('extrude.path.line', new THREE.ExtrudeGeometry(disc, { steps: 2, extrudePath: new THREE.LineCurve3(pts3[0], pts3[1]), curveSegments: 6 }));
  const copyShape = disc.clone();
  copyShape.holes.pop();
  P('shape.clone', copyShape.holes.length, disc.holes.length, copyShape.holes[0] !== disc.holes[0], copyShape.curves.length);

  // --- tubes
  GS('tube', new THREE.TubeGeometry(new THREE.CatmullRomCurve3(pts3), 16, 0.017, 5, false));
  GS('tube.closed', new THREE.TubeGeometry(new THREE.CatmullRomCurve3(pts3, true), 20, 0.2, 6, true));
  GS('tube.line', new THREE.TubeGeometry(new THREE.LineCurve3(pts3[2], pts3[3]), 3, 0.5, 4));
  GS('tube.default', new THREE.TubeGeometry(new THREE.QuadraticBezierCurve3(new THREE.Vector3(-1, -1, 0), new THREE.Vector3(-1, 1, 0), new THREE.Vector3(1, 1, 0))));

  // --- triangulation
  const faces = (label, f) => row(label, f.flat(), 60);
  {
    const square = [new THREE.Vector2(0, 0), new THREE.Vector2(1, 0), new THREE.Vector2(1, 1), new THREE.Vector2(0, 1)];
    faces('tri.square', THREE.ShapeUtils.triangulateShape(square, []));
    // the end like the beginning: taken off; points in a line; a point twice
    const closed = [new THREE.Vector2(0, 0), new THREE.Vector2(1, 0), new THREE.Vector2(2, 0), new THREE.Vector2(2, 1), new THREE.Vector2(2, 2), new THREE.Vector2(1, 2), new THREE.Vector2(1, 2),
      new THREE.Vector2(0, 2), new THREE.Vector2(0, 1), new THREE.Vector2(0, 0)];
    faces('tri.closed', THREE.ShapeUtils.triangulateShape(closed, []));
    P('tri.closed.length', closed.length);
    const lShape = [new THREE.Vector2(0, 0), new THREE.Vector2(3, 0), new THREE.Vector2(3, 1), new THREE.Vector2(1, 1), new THREE.Vector2(1, 3), new THREE.Vector2(0, 3)];
    faces('tri.L', THREE.ShapeUtils.triangulateShape(lShape, []));
    faces('tri.L.reversed', THREE.ShapeUtils.triangulateShape(lShape.slice().reverse(), []));
    const frame = [new THREE.Vector2(0, 0), new THREE.Vector2(4, 0), new THREE.Vector2(4, 4), new THREE.Vector2(0, 4)];
    const frameHoles = [[new THREE.Vector2(1, 1), new THREE.Vector2(1, 2), new THREE.Vector2(2, 2), new THREE.Vector2(2, 1)],
      [new THREE.Vector2(2.5, 2.5), new THREE.Vector2(2.5, 3.5), new THREE.Vector2(3.5, 3.5), new THREE.Vector2(3.5, 2.5), new THREE.Vector2(2.5, 2.5)],
      [new THREE.Vector2(1, 2.5), new THREE.Vector2(1.5, 3.5), new THREE.Vector2(2, 2.5)]];
    faces('tri.frame', THREE.ShapeUtils.triangulateShape(frame, frameHoles));
    P('tri.frame.holes', frameHoles.map((h) => h.length), THREE.ShapeUtils.area(frame), THREE.ShapeUtils.area(frameHoles[0]), THREE.ShapeUtils.isClockWise(frameHoles[0]));
    faces('tri.degenerate', THREE.ShapeUtils.triangulateShape([new THREE.Vector2(0, 0), new THREE.Vector2(1, 1)], []));
    // stars: an outline that goes in and out about a middle, with two holes; the larger ones are sorted along a curve first
    for (const n of [7, 30, 30, 120, 120, 400]) {
      const star = [];
      for (let i = 0; i < n; i++) {
        const a = (i + rnd() * 0.8) / n * PI * 2, rad = r(5, 10);
        star.push(new THREE.Vector2(Math.cos(a) * rad, Math.sin(a) * rad));
      }
      const holes = [];
      for (const [cx, cy] of [[-2, 0], [2, 0.5]]) {
        const hole = [], m = 5 + (n % 7);
        for (let i = 0; i < m; i++) {
          const a = -(i + rnd() * 0.8) / m * PI * 2, rad = r(0.3, 1);
          hole.push(new THREE.Vector2(cx + Math.cos(a) * rad, cy + Math.sin(a) * rad));
        }
        holes.push(hole);
      }
      faces('tri.star.' + n, THREE.ShapeUtils.triangulateShape(star.slice(), []));
      faces('tri.star.' + n + '.holes', THREE.ShapeUtils.triangulateShape(star, holes));
      // on a grid: many points in lines and with the same x
      const snapped = star.map((p) => new THREE.Vector2(Math.round(p.x), Math.round(p.y)));
      faces('tri.star.' + n + '.grid', THREE.ShapeUtils.triangulateShape(snapped, []));
    }
    row('tri.earcut', Earcut.triangulate([0, 0, 10, 0, 10, 10, 0, 10, 2, 2, 2, 4, 4, 4, 4, 2, 6, 6, 6, 8, 8, 8, 8, 6], [4, 8]), 60);
    row('tri.earcut.steiner', Earcut.triangulate([0, 0, 10, 0, 10, 10, 0, 10, 5, 5, 3, 3], [4, 5]), 60);
    row('tri.earcut.empty', Earcut.triangulate([]), 6);
  }

  // --- merging
  {
    const parts = [new THREE.BoxGeometry(1, 2, 3).translate(0, 1, 0), new THREE.CylinderGeometry(0.5, 0.5, 2, 8).rotateZ(PI / 2), new THREE.SphereGeometry(0.7, 8, 6).translate(2, 0, 0)];
    GS('merge', mergeGeometries(parts, false));
    GS('merge.groups', mergeGeometries(parts, true));
    const bare = parts.map((g) => g.toNonIndexed());
    GS('merge.bare', mergeGeometries(bare, false));
    GS('merge.bare.groups', mergeGeometries(bare, true));
    P('merge.null', mergeGeometries([parts[0], bare[1]]) === null, mergeGeometries([parts[0], new THREE.BoxGeometry().deleteAttribute('uv')]) === null,
      mergeGeometries([new THREE.BoxGeometry().deleteAttribute('uv'), parts[0]]) === null);
    // more than 65535 vertices: an index of 32 bits
    const many = [];
    for (let i = 0; i < 8; i++) many.push(new THREE.PlaneGeometry(1, 1, 95, 95));
    const big = mergeGeometries(many);
    P('merge.big', big.index.array.constructor.name, big.index.count, big.attributes.position.count, sums(big.index.array));
    // vertices that are alike become one
    const welded = mergeVertices(new THREE.BoxGeometry(1, 2, 3));
    GS('mergeVertices', welded);
    const positionsOnly = new THREE.BoxGeometry(1, 2, 3, 2, 2, 2).toNonIndexed();
    positionsOnly.deleteAttribute('normal');
    positionsOnly.deleteAttribute('uv');
    GS('mergeVertices.positions', mergeVertices(positionsOnly));
    GS('mergeVertices.tolerance', mergeVertices(new THREE.SphereGeometry(1, 12, 8), 0.3));
    const smoothed = mergeVertices(positionsOnly);
    smoothed.computeVertexNormals();
    GS('mergeVertices.normals', smoothed);
  }
}

// ---------------------------------------------------------------------------

const sections = { math: mathSection, scene: sceneSection, geom: geomSection };
const wanted = process.argv.slice(2).length ? process.argv.slice(2) : Object.keys(sections);
for (const name of wanted) {
  if (!sections[name]) throw new Error('no such section: ' + name);
  lines.push('# ' + name);
  sections[name]();
}
process.stdout.write(lines.join('\n') + '\n');
