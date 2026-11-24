// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Math.sin, cos, tan, atan, atan2, acos, exp and tanh as fdlibm defines them, in plain
 * JavaScript arithmetic – the same operations in the same order as src/sim/jsmath.cpp.
 *
 * Why: an engine's own Math functions are compiled C++, and where the compiler may fuse a
 * multiplication and an addition into one instruction (node on AArch64 as most distributions
 * build it) their last bit differs from the same engine's on other machines – in about one
 * call of two hundred.  A comparison bit by bit needs both sides to compute the same thing:
 * `strictMath()` replaces the eight functions for the run of a comparison script.  (Chrome is
 * built without fused operations, so this is also what the game computes in the browser.)
 * Math.hypot, Math.sqrt and Math.pow are left as they are: the engine's own are exact
 * (hypot, sqrt) or the C library's (pow), as in the C++.
 */

const f64 = new Float64Array(1);
const u32 = new Uint32Array(f64.buffer);
// (little-endian, as every machine this runs on: u32[1] is the high word)
function hi(x: number): number {
  f64[0] = x;
  return u32[1] | 0;
}
function lo(x: number): number {
  f64[0] = x;
  return u32[0];
}
function words(h: number, l: number): number {
  u32[1] = h >>> 0;
  u32[0] = l >>> 0;
  return f64[0];
}
function withHi(x: number, h: number): number {
  f64[0] = x;
  u32[1] = h >>> 0;
  return f64[0];
}
function withLo(x: number, l: number): number {
  f64[0] = x;
  u32[0] = l >>> 0;
  return f64[0];
}
/** 2^n for a result that is a normal number */
function pow2(n: number): number {
  return words((0x3ff + n) << 20, 0);
}
/** z · 2^n as scalbn does where nothing under- or overflows */
function scalbn(z: number, n: number): number {
  return z * pow2(n);
}

function kernelSin(x: number, y: number, iy: number): number {
  const half = 5.00000000000000000000e-01,
    S1 = -1.66666666666666324348e-01,
    S2 = 8.33333333332248946124e-03,
    S3 = -1.98412698298579493134e-04,
    S4 = 2.75573137070700676789e-06,
    S5 = -2.50507602534068634195e-08,
    S6 = 1.58969099521155010221e-10;
  const ix = hi(x) & 0x7fffffff;
  if (ix < 0x3e400000) {
    if ((x | 0) === 0) return x;
  }
  const z = x * x;
  const v = z * x;
  const r = S2 + z * (S3 + z * (S4 + z * (S5 + z * S6)));
  if (iy === 0) return x + v * (S1 + z * r);
  return x - ((z * (half * y - v * r) - y) - v * S1);
}

function kernelCos(x: number, y: number): number {
  const one = 1.0,
    C1 = 4.16666666666666019037e-02,
    C2 = -1.38888888888741095749e-03,
    C3 = 2.48015872894767294178e-05,
    C4 = -2.75573143513906633035e-07,
    C5 = 2.08757232129817482790e-09,
    C6 = -1.13596475577881948265e-11;
  const ix = hi(x) & 0x7fffffff;
  if (ix < 0x3e400000) {
    if ((x | 0) === 0) return one;
  }
  const z = x * x;
  const r = z * (C1 + z * (C2 + z * (C3 + z * (C4 + z * (C5 + z * C6)))));
  if (ix < 0x3fd33333) return one - (0.5 * z - (z * r - x * y));
  let qx: number;
  if (ix > 0x3fe90000) qx = 0.28125;
  else qx = words(ix - 0x00200000, 0);
  const iz = 0.5 * z - qx;
  const a = one - qx;
  return a - (iz - (z * r - x * y));
}

const T = [
  3.33333333333334091986e-01, 1.33333333333201242699e-01, 5.39682539762260521377e-02, 2.18694882948595424599e-02,
  8.86323982359930005737e-03, 3.59207910759131235356e-03, 1.45620945432529025516e-03, 5.88041240820264096874e-04,
  2.46463134818469906812e-04, 7.81794442939557092300e-05, 7.14072491382608190305e-05, -1.85586374855275456654e-05,
  2.59073051863633712884e-05,
];

function kernelTan(x: number, y: number, iy: number): number {
  const one = 1.0,
    pio4 = 7.85398163397448278999e-01,
    pio4lo = 3.06161699786838301793e-17;
  let z: number, r: number, v: number, w: number, s: number;
  const hx = hi(x);
  const ix = hx & 0x7fffffff;
  if (ix < 0x3e300000) {
    if ((x | 0) === 0) {
      if (((ix | lo(x)) | (iy + 1)) === 0) return one / Math.abs(x);
      else if (iy === 1) return x;
      else {
        z = w = x + y;
        z = withLo(z, 0);
        v = y - (z - x);
        const a = -one / w;
        let t = a;
        t = withLo(t, 0);
        s = one + t * z;
        return t + a * (s + t * v);
      }
    }
  }
  if (ix >= 0x3fe59428) {
    if (hx < 0) {
      x = -x;
      y = -y;
    }
    z = pio4 - x;
    w = pio4lo - y;
    x = z + w;
    y = 0.0;
  }
  z = x * x;
  w = z * z;
  r = T[1] + w * (T[3] + w * (T[5] + w * (T[7] + w * (T[9] + w * T[11]))));
  v = z * (T[2] + w * (T[4] + w * (T[6] + w * (T[8] + w * (T[10] + w * T[12])))));
  s = z * x;
  r = y + z * (s * (r + v) + y);
  r += T[0] * s;
  w = x + r;
  if (ix >= 0x3fe59428) {
    v = iy;
    return (1 - ((hx >> 30) & 2)) * (v - 2.0 * (x - ((w * w) / (w + v) - r)));
  }
  if (iy === 1) return w;
  z = w;
  z = withLo(z, 0);
  v = r - (z - x);
  const a = -1.0 / w;
  let t = a;
  t = withLo(t, 0);
  s = 1.0 + t * z;
  return t + a * (s + t * v);
}

const two_over_pi = [
  0xa2f983, 0x6e4e44, 0x1529fc, 0x2757d1, 0xf534dd, 0xc0db62, 0x95993c, 0x439041, 0xfe5163, 0xabdebb, 0xc561b7,
  0x246e3a, 0x424dd2, 0xe00649, 0x2eea09, 0xd1921c, 0xfe1deb, 0x1cb129, 0xa73ee8, 0x8235f5, 0x2ebb44, 0x84e99c,
  0x7026b4, 0x5f7e41, 0x3991d6, 0x398353, 0x39f49c, 0x845f8b, 0xbdf928, 0x3b1ff8, 0x97ffde, 0x05980f, 0xef2f11,
  0x8b5a0a, 0x6d1f6d, 0x367ecf, 0x27cb09, 0xb74f46, 0x3f669e, 0x5fea2d, 0x7527ba, 0xc7ebe5, 0xf17b3d, 0x0739f7,
  0x8a5292, 0xea6bfb, 0x5fb11f, 0x8d5d08, 0x560330, 0x46fc7b, 0x6babf0, 0xcfbc20, 0x9af436, 0x1da9e3, 0x91615e,
  0xe61b08, 0x659985, 0x5f14a0, 0x68408d, 0xffd880, 0x4d7327, 0x310606, 0x1556ca, 0x73a8c9, 0x60e27b, 0xc08c6b,
];
const PIo2 = [
  1.57079625129699707031e+00, 7.54978941586159635335e-08, 5.39030252995776476554e-15, 3.28200341580791294123e-22,
  1.27065575308067607349e-28, 1.22933308981111328932e-36, 2.73370053816464559624e-44, 2.16741683877804819444e-51,
];

function kernelRemPio2(x: number[], y: number[], e0: number, nx: number): number {
  const two24 = 1.67772160000000000000e+07,
    twon24 = 5.96046447753906250000e-08;
  const iq: number[] = new Array(20).fill(0);
  const f: number[] = new Array(20).fill(0);
  const fq: number[] = new Array(20).fill(0);
  const q: number[] = new Array(20).fill(0);
  let jz: number, carry: number, n: number, i: number, j: number, k: number, ih: number, z: number, fw: number;
  const jk = 4;
  const jp = jk;
  const jx = nx - 1;
  let jv = ((e0 - 3) / 24) | 0;
  if (jv < 0) jv = 0;
  let q0 = e0 - 24 * (jv + 1);
  j = jv - jx;
  const m = jx + jk;
  for (i = 0; i <= m; i++, j++) f[i] = j < 0 ? 0 : two_over_pi[j];
  for (i = 0; i <= jk; i++) {
    for (j = 0, fw = 0.0; j <= jx; j++) fw += x[j] * f[jx + i - j];
    q[i] = fw;
  }
  jz = jk;
  for (;;) {
    for (i = 0, j = jz, z = q[jz]; j > 0; i++, j--) {
      fw = (twon24 * z) | 0;
      iq[i] = (z - two24 * fw) | 0;
      z = q[j - 1] + fw;
    }
    z = scalbn(z, q0);
    z -= 8.0 * Math.floor(z * 0.125);
    n = z | 0;
    z -= n;
    ih = 0;
    if (q0 > 0) {
      i = iq[jz - 1] >> (24 - q0);
      n += i;
      iq[jz - 1] -= i << (24 - q0);
      ih = iq[jz - 1] >> (23 - q0);
    } else if (q0 === 0) ih = iq[jz - 1] >> 23;
    else if (z >= 0.5) ih = 2;
    if (ih > 0) {
      n += 1;
      carry = 0;
      for (i = 0; i < jz; i++) {
        j = iq[i];
        if (carry === 0) {
          if (j !== 0) {
            carry = 1;
            iq[i] = 0x1000000 - j;
          }
        } else iq[i] = 0xffffff - j;
      }
      if (q0 > 0) {
        if (q0 === 1) iq[jz - 1] &= 0x7fffff;
        else if (q0 === 2) iq[jz - 1] &= 0x3fffff;
      }
      if (ih === 2) {
        z = 1.0 - z;
        if (carry !== 0) z -= scalbn(1.0, q0);
      }
    }
    if (z === 0) {
      j = 0;
      for (i = jz - 1; i >= jk; i--) j |= iq[i];
      if (j === 0) {
        for (k = 1; iq[jk - k] === 0; k++) {
          /* k = number of terms needed */
        }
        for (i = jz + 1; i <= jz + k; i++) {
          f[jx + i] = two_over_pi[jv + i];
          for (j = 0, fw = 0.0; j <= jx; j++) fw += x[j] * f[jx + i - j];
          q[i] = fw;
        }
        jz += k;
        continue;
      }
    }
    break;
  }
  if (z === 0.0) {
    jz -= 1;
    q0 -= 24;
    while (iq[jz] === 0) {
      jz--;
      q0 -= 24;
    }
  } else {
    z = scalbn(z, -q0);
    if (z >= two24) {
      fw = (twon24 * z) | 0;
      iq[jz] = (z - two24 * fw) | 0;
      jz += 1;
      q0 += 24;
      iq[jz] = fw | 0;
    } else iq[jz] = z | 0;
  }
  fw = scalbn(1.0, q0);
  for (i = jz; i >= 0; i--) {
    q[i] = fw * iq[i];
    fw *= twon24;
  }
  for (i = jz; i >= 0; i--) {
    for (fw = 0.0, k = 0; k <= jp && k <= jz - i; k++) fw += PIo2[k] * q[i + k];
    fq[jz - i] = fw;
  }
  fw = 0.0;
  for (i = jz; i >= 0; i--) fw += fq[i];
  y[0] = ih === 0 ? fw : -fw;
  fw = fq[0] - fw;
  for (i = 1; i <= jz; i++) fw += fq[i];
  y[1] = ih === 0 ? fw : -fw;
  return n & 7;
}

const npio2_hw = [
  0x3ff921fb, 0x400921fb, 0x4012d97c, 0x401921fb, 0x401f6a7a, 0x4022d97c, 0x4025fdbb, 0x402921fb, 0x402c463a,
  0x402f6a7a, 0x4031475c, 0x4032d97c, 0x40346b9c, 0x4035fdbb, 0x40378fdb, 0x403921fb, 0x403ab41b, 0x403c463a,
  0x403dd85a, 0x403f6a7a, 0x40407e4c, 0x4041475c, 0x4042106c, 0x4042d97c, 0x4043a28c, 0x40446b9c, 0x404534ac,
  0x4045fdbb, 0x4046c6cb, 0x40478fdb, 0x404858eb, 0x404921fb,
];

function remPio2(x: number, y: number[]): number {
  const half = 0.5,
    two24 = 1.67772160000000000000e+07,
    invpio2 = 6.36619772367581382433e-01,
    pio2_1 = 1.57079632673412561417e+00,
    pio2_1t = 6.07710050650619224932e-11,
    pio2_2 = 6.07710050630396597660e-11,
    pio2_2t = 2.02226624879595063154e-21,
    pio2_3 = 2.02226624871116645580e-21,
    pio2_3t = 8.47842766036889956997e-32;
  let z: number, w: number, t: number, r: number, i: number, n: number;
  const hx = hi(x);
  const ix = hx & 0x7fffffff;
  if (ix <= 0x3fe921fb) {
    y[0] = x;
    y[1] = 0;
    return 0;
  }
  if (ix < 0x4002d97c) {
    if (hx > 0) {
      z = x - pio2_1;
      if (ix !== 0x3ff921fb) {
        y[0] = z - pio2_1t;
        y[1] = z - y[0] - pio2_1t;
      } else {
        z -= pio2_2;
        y[0] = z - pio2_2t;
        y[1] = z - y[0] - pio2_2t;
      }
      return 1;
    } else {
      z = x + pio2_1;
      if (ix !== 0x3ff921fb) {
        y[0] = z + pio2_1t;
        y[1] = z - y[0] + pio2_1t;
      } else {
        z += pio2_2;
        y[0] = z + pio2_2t;
        y[1] = z - y[0] + pio2_2t;
      }
      return -1;
    }
  }
  if (ix <= 0x413921fb) {
    t = Math.abs(x);
    n = (t * invpio2 + half) | 0;
    const fn = n;
    r = t - fn * pio2_1;
    w = fn * pio2_1t;
    if (n < 32 && ix !== npio2_hw[n - 1]) {
      y[0] = r - w;
    } else {
      const j = ix >> 20;
      y[0] = r - w;
      i = j - ((hi(y[0]) >>> 20) & 0x7ff);
      if (i > 16) {
        t = r;
        w = fn * pio2_2;
        r = t - w;
        w = fn * pio2_2t - (t - r - w);
        y[0] = r - w;
        i = j - ((hi(y[0]) >>> 20) & 0x7ff);
        if (i > 49) {
          t = r;
          w = fn * pio2_3;
          r = t - w;
          w = fn * pio2_3t - (t - r - w);
          y[0] = r - w;
        }
      }
    }
    y[1] = r - y[0] - w;
    if (hx < 0) {
      y[0] = -y[0];
      y[1] = -y[1];
      return -n;
    }
    return n;
  }
  if (ix >= 0x7ff00000) {
    y[0] = y[1] = x - x;
    return 0;
  }
  const low = lo(x);
  const e0 = (ix >> 20) - 1046;
  z = words(ix - (e0 << 20), low);
  const tx = [0, 0, 0];
  for (i = 0; i < 2; i++) {
    tx[i] = z | 0;
    z = (z - tx[i]) * two24;
  }
  tx[2] = z;
  let nx = 3;
  while (tx[nx - 1] === 0) nx--;
  n = kernelRemPio2(tx, y, e0, nx);
  if (hx < 0) {
    y[0] = -y[0];
    y[1] = -y[1];
    return -n;
  }
  return n;
}

const yy = [0, 0];

export function sin(x: number): number {
  x = +x;
  const ix = hi(x) & 0x7fffffff;
  if (ix <= 0x3fe921fb) return kernelSin(x, 0.0, 0);
  if (ix >= 0x7ff00000) return x - x;
  const n = remPio2(x, yy);
  switch (n & 3) {
    case 0:
      return kernelSin(yy[0], yy[1], 1);
    case 1:
      return kernelCos(yy[0], yy[1]);
    case 2:
      return -kernelSin(yy[0], yy[1], 1);
    default:
      return -kernelCos(yy[0], yy[1]);
  }
}

export function cos(x: number): number {
  x = +x;
  const ix = hi(x) & 0x7fffffff;
  if (ix <= 0x3fe921fb) return kernelCos(x, 0.0);
  if (ix >= 0x7ff00000) return x - x;
  const n = remPio2(x, yy);
  switch (n & 3) {
    case 0:
      return kernelCos(yy[0], yy[1]);
    case 1:
      return -kernelSin(yy[0], yy[1], 1);
    case 2:
      return -kernelCos(yy[0], yy[1]);
    default:
      return kernelSin(yy[0], yy[1], 1);
  }
}

export function tan(x: number): number {
  x = +x;
  const ix = hi(x) & 0x7fffffff;
  if (ix <= 0x3fe921fb) return kernelTan(x, 0.0, 1);
  if (ix >= 0x7ff00000) return x - x;
  const n = remPio2(x, yy);
  return kernelTan(yy[0], yy[1], 1 - ((n & 1) << 1));
}

const atanhi = [4.63647609000806093515e-01, 7.85398163397448278999e-01, 9.82793723247329054082e-01, 1.57079632679489655800e+00];
const atanlo = [2.26987774529616870924e-17, 3.06161699786838301793e-17, 1.39033110312309984516e-17, 6.12323399573676603587e-17];
const aT = [
  3.33333333333329318027e-01, -1.99999999998764832476e-01, 1.42857142725034663711e-01, -1.11111104054623557880e-01,
  9.09088713343650656196e-02, -7.69187620504482999495e-02, 6.66107313738753120669e-02, -5.83357013379057348645e-02,
  4.97687799461593236017e-02, -3.65315727442169155270e-02, 1.62858201153657823623e-02,
];

export function atan(x: number): number {
  x = +x;
  const one = 1.0,
    huge = 1.0e300;
  let id: number;
  const hx = hi(x);
  const ix = hx & 0x7fffffff;
  if (ix >= 0x44100000) {
    if (ix > 0x7ff00000 || (ix === 0x7ff00000 && lo(x) !== 0)) return x + x;
    if (hx > 0) return atanhi[3] + atanlo[3];
    return -atanhi[3] - atanlo[3];
  }
  if (ix < 0x3fdc0000) {
    if (ix < 0x3e200000) {
      if (huge + x > one) return x;
    }
    id = -1;
  } else {
    x = Math.abs(x);
    if (ix < 0x3ff30000) {
      if (ix < 0x3fe60000) {
        id = 0;
        x = (2.0 * x - one) / (2.0 + x);
      } else {
        id = 1;
        x = (x - one) / (x + one);
      }
    } else {
      if (ix < 0x40038000) {
        id = 2;
        x = (x - 1.5) / (one + 1.5 * x);
      } else {
        id = 3;
        x = -1.0 / x;
      }
    }
  }
  let z = x * x;
  const w = z * z;
  const s1 = z * (aT[0] + w * (aT[2] + w * (aT[4] + w * (aT[6] + w * (aT[8] + w * aT[10])))));
  const s2 = w * (aT[1] + w * (aT[3] + w * (aT[5] + w * (aT[7] + w * aT[9]))));
  if (id < 0) return x - x * (s1 + s2);
  z = atanhi[id] - (x * (s1 + s2) - atanlo[id] - x);
  return hx < 0 ? -z : z;
}

export function atan2(y: number, x: number): number {
  y = +y;
  x = +x;
  const tiny = 1.0e-300,
    pi_o_4 = 7.85398163397448279e-01,
    pi_o_2 = 1.570796326794896558e+00,
    pi = 3.141592653589793116e+00,
    pi_lo = 1.2246467991473531772e-16;
  let z: number;
  const hx = hi(x);
  const lx = lo(x);
  const ix = hx & 0x7fffffff;
  const hy = hi(y);
  const ly = lo(y);
  const iy = hy & 0x7fffffff;
  if (x !== x || y !== y) return x + y;
  if ((((hx - 0x3ff00000) | 0) | lx) === 0) return atan(y);
  let m = ((hy >> 31) & 1) | ((hx >> 30) & 2);
  if ((iy | ly) === 0) {
    switch (m) {
      case 0:
      case 1:
        return y;
      case 2:
        return pi + tiny;
      case 3:
        return -pi - tiny;
    }
  }
  if ((ix | lx) === 0) return hy < 0 ? -pi_o_2 - tiny : pi_o_2 + tiny;
  if (ix === 0x7ff00000) {
    if (iy === 0x7ff00000) {
      switch (m) {
        case 0:
          return pi_o_4 + tiny;
        case 1:
          return -pi_o_4 - tiny;
        case 2:
          return 3.0 * pi_o_4 + tiny;
        case 3:
          return -3.0 * pi_o_4 - tiny;
      }
    } else {
      switch (m) {
        case 0:
          return 0;
        case 1:
          return -0;
        case 2:
          return pi + tiny;
        case 3:
          return -pi - tiny;
      }
    }
  }
  if (iy === 0x7ff00000) return hy < 0 ? -pi_o_2 - tiny : pi_o_2 + tiny;
  const k = (iy - ix) >> 20;
  if (k > 60) {
    z = pi_o_2 + 0.5 * pi_lo;
    m &= 1;
  } else if (hx < 0 && k < -60) z = 0.0;
  else z = atan(Math.abs(y / x));
  switch (m) {
    case 0:
      return z;
    case 1:
      return -z;
    case 2:
      return pi - (z - pi_lo);
    default:
      return z - pi_lo - pi;
  }
}

export function acos(x: number): number {
  x = +x;
  const one = 1.0,
    pi = 3.14159265358979311600e+00,
    pio2_hi = 1.57079632679489655800e+00,
    pio2_lo = 6.12323399573676603587e-17,
    pS0 = 1.66666666666666657415e-01,
    pS1 = -3.25565818622400915405e-01,
    pS2 = 2.01212532134862925881e-01,
    pS3 = -4.00555345006794114027e-02,
    pS4 = 7.91534994289814532176e-04,
    pS5 = 3.47933107596021167570e-05,
    qS1 = -2.40339491173441421878e+00,
    qS2 = 2.02094576023350569471e+00,
    qS3 = -6.88283971605453293030e-01,
    qS4 = 7.70381505559019352791e-02;
  let z: number, p: number, q: number, r: number, w: number, s: number;
  const hx = hi(x);
  const ix = hx & 0x7fffffff;
  if (ix >= 0x3ff00000) {
    if (((ix - 0x3ff00000) | lo(x)) === 0) {
      if (hx > 0) return 0.0;
      return pi + 2.0 * pio2_lo;
    }
    return NaN;
  }
  if (ix < 0x3fe00000) {
    if (ix <= 0x3c600000) return pio2_hi + pio2_lo;
    z = x * x;
    p = z * (pS0 + z * (pS1 + z * (pS2 + z * (pS3 + z * (pS4 + z * pS5)))));
    q = one + z * (qS1 + z * (qS2 + z * (qS3 + z * qS4)));
    r = p / q;
    return pio2_hi - (x - (pio2_lo - r * x));
  } else if (hx < 0) {
    z = (one + x) * 0.5;
    p = z * (pS0 + z * (pS1 + z * (pS2 + z * (pS3 + z * (pS4 + z * pS5)))));
    q = one + z * (qS1 + z * (qS2 + z * (qS3 + z * qS4)));
    s = Math.sqrt(z);
    r = p / q;
    w = r * s - pio2_lo;
    return pi - 2.0 * (s + w);
  } else {
    z = (one - x) * 0.5;
    s = Math.sqrt(z);
    let df = s;
    df = withLo(df, 0);
    const c = (z - df * df) / (s + df);
    p = z * (pS0 + z * (pS1 + z * (pS2 + z * (pS3 + z * (pS4 + z * pS5)))));
    q = one + z * (qS1 + z * (qS2 + z * (qS3 + z * qS4)));
    r = p / q;
    w = r * s + c;
    return 2.0 * (df + w);
  }
}

export function exp(x: number): number {
  x = +x;
  const one = 1.0,
    o_threshold = 7.09782712893383973096e+02,
    u_threshold = -7.45133219101941108420e+02,
    ln2HI = 6.93147180369123816490e-01,
    ln2LO = 1.90821492927058770002e-10,
    invln2 = 1.44269504088896338700e+00,
    P1 = 1.66666666666666019037e-01,
    P2 = -2.77777777770155933842e-03,
    P3 = 6.61375632143793436117e-05,
    P4 = -1.65339022054652515390e-06,
    P5 = 4.13813679705723846039e-08,
    E = 2.718281828459045,
    huge = 1.0e+300,
    twom1000 = 9.33263618503218878990e-302,
    two1023 = 8.988465674311579539e307;
  let hi_ = 0.0,
    lo_ = 0.0,
    t: number,
    k = 0;
  let hx = hi(x) >>> 0;
  const xsb = (hx >>> 31) & 1;
  hx &= 0x7fffffff;
  if (hx >= 0x40862e42) {
    if (hx >= 0x7ff00000) {
      if (((hx & 0xfffff) | lo(x)) !== 0) return x + x;
      return xsb === 0 ? x : 0.0;
    }
    if (x > o_threshold) return huge * huge;
    if (x < u_threshold) return twom1000 * twom1000;
  }
  if (hx > 0x3fd62e42) {
    if (hx < 0x3ff0a2b2) {
      if (x === 1.0) return E;
      hi_ = x - (xsb ? -ln2HI : ln2HI);
      lo_ = xsb ? -ln2LO : ln2LO;
      k = 1 - xsb - xsb;
    } else {
      k = (invln2 * x + (xsb ? -0.5 : 0.5)) | 0;
      t = k;
      hi_ = x - t * ln2HI;
      lo_ = t * ln2LO;
    }
    x = hi_ - lo_;
  } else if (hx < 0x3e300000) {
    if (huge + x > one) return one + x;
    k = 0;
  } else k = 0;
  t = x * x;
  const twopk = k >= -1021 ? words(0x3ff00000 + (k << 20), 0) : words(0x3ff00000 + ((k + 1000) << 20), 0);
  const c = x - t * (P1 + t * (P2 + t * (P3 + t * (P4 + t * P5))));
  if (k === 0) return one - ((x * c) / (c - 2.0) - x);
  const y = one - (lo_ - (x * c) / (2.0 - c) - hi_);
  if (k >= -1021) {
    if (k === 1024) return y * 2.0 * two1023;
    return y * twopk;
  }
  return y * twopk * twom1000;
}

function expm1(x: number): number {
  const one = 1.0,
    tiny = 1.0e-300,
    huge = 1.0e+300,
    o_threshold = 7.09782712893383973096e+02,
    ln2_hi = 6.93147180369123816490e-01,
    ln2_lo = 1.90821492927058770002e-10,
    invln2 = 1.44269504088896338700e+00,
    Q1 = -3.33333333333331316428e-02,
    Q2 = 1.58730158725481460165e-03,
    Q3 = -7.93650757867487942473e-05,
    Q4 = 4.00821782732936239552e-06,
    Q5 = -2.01099218183624371326e-07;
  let y: number, hi_: number, lo_: number, c = 0, t: number, e: number, k: number;
  let hx = hi(x) >>> 0;
  const xsb = hx & 0x80000000;
  hx &= 0x7fffffff;
  if (hx >= 0x4043687a) {
    if (hx >= 0x40862e42) {
      if (hx >= 0x7ff00000) {
        if (((hx & 0xfffff) | lo(x)) !== 0) return x + x;
        return xsb === 0 ? x : -1.0;
      }
      if (x > o_threshold) return huge * huge;
    }
    if (xsb !== 0) {
      if (x + tiny < 0.0) return tiny - one;
    }
  }
  if (hx > 0x3fd62e42) {
    if (hx < 0x3ff0a2b2) {
      if (xsb === 0) {
        hi_ = x - ln2_hi;
        lo_ = ln2_lo;
        k = 1;
      } else {
        hi_ = x + ln2_hi;
        lo_ = -ln2_lo;
        k = -1;
      }
    } else {
      k = (invln2 * x + (xsb === 0 ? 0.5 : -0.5)) | 0;
      t = k;
      hi_ = x - t * ln2_hi;
      lo_ = t * ln2_lo;
    }
    x = hi_ - lo_;
    c = hi_ - x - lo_;
  } else if (hx < 0x3c900000) {
    t = huge + x;
    return x - (t - (huge + x));
  } else k = 0;
  const hfx = 0.5 * x;
  const hxs = x * hfx;
  const r1 = one + hxs * (Q1 + hxs * (Q2 + hxs * (Q3 + hxs * (Q4 + hxs * Q5))));
  t = 3.0 - r1 * hfx;
  e = hxs * ((r1 - t) / (6.0 - x * t));
  if (k === 0) return x - (x * e - hxs);
  const twopk = words(0x3ff00000 + (k << 20), 0);
  e = x * (e - c) - c;
  e -= hxs;
  if (k === -1) return 0.5 * (x - e) - 0.5;
  if (k === 1) {
    if (x < -0.25) return -2.0 * (e - (x + 0.5));
    return one + 2.0 * (x - e);
  }
  if (k <= -2 || k > 56) {
    y = one - (e - x);
    if (k === 1024) y = y * 2.0 * 8.98846567431158e+307;
    else y = y * twopk;
    return y - one;
  }
  t = one;
  if (k < 20) {
    t = withHi(t, 0x3ff00000 - (0x200000 >> k));
    y = t - (e - x);
    y = y * twopk;
  } else {
    t = withHi(t, (0x3ff - k) << 20);
    y = x - (e + t);
    y += one;
    y = y * twopk;
  }
  return y;
}

export function tanh(x: number): number {
  x = +x;
  const one = 1.0,
    two = 2.0,
    tiny = 1.0e-300,
    huge = 1.0e300;
  let t: number, z: number;
  const jx = hi(x);
  const ix = jx & 0x7fffffff;
  if (ix >= 0x7ff00000) {
    if (jx >= 0) return one / x + one;
    return one / x - one;
  }
  if (ix < 0x40360000) {
    if (ix < 0x3e300000) {
      if (huge + x > one) return x;
    }
    if (ix >= 0x3ff00000) {
      t = expm1(two * Math.abs(x));
      z = one - two / (t + two);
    } else {
      t = expm1(-two * Math.abs(x));
      z = -t / (t + two);
    }
  } else z = one - tiny;
  return jx >= 0 ? z : -z;
}

/** Replace the engine's eight functions (for the whole process: call it before the simulation runs). */
export function strictMath(): void {
  Math.sin = sin;
  Math.cos = cos;
  Math.tan = tan;
  Math.atan = atan;
  Math.atan2 = atan2;
  Math.acos = acos;
  Math.exp = exp;
  Math.tanh = tanh;
}
