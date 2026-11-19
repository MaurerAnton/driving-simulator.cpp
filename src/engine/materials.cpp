// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Materials: the procedural shader sources of the game.
 *
 * Origin: src/render/materials.ts (MaurerAnton/driving-simulator @ 5b6ddc7,
 * v0.1.0).  The GLSL is carried over verbatim; only the three.js `#include`
 * plumbing is spelled out as the WebGL2 backend will compile it.
 */
#include "engine/materials.h"

namespace fs {

// ---------------------------------------------------------------------------
// Shared GLSL (verbatim)
// ---------------------------------------------------------------------------

const char* const NOISE_GLSL = R"GLSL(
float fsHash(vec2 p) {
  p = fract(p * vec2(123.34, 456.21));
  p += dot(p, p + 45.32);
  return fract(p.x * p.y);
}
float fsNoise(vec2 p) {
  vec2 i = floor(p);
  vec2 f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  return mix(mix(fsHash(i), fsHash(i + vec2(1.0, 0.0)), f.x),
             mix(fsHash(i + vec2(0.0, 1.0)), fsHash(i + vec2(1.0, 1.0)), f.x), f.y);
}
vec3 fsLin(vec3 c) { return pow(c, vec3(2.2)); }
)GLSL";

const char* const GROUND_GLSL = R"GLSL(
// joint mask of a rectangular paving pattern, 1 = joint
float fsJoints(vec2 p, vec2 size, float bond, float w) {
  vec2 g = p / size;
  g.x += bond * floor(g.y);
  vec2 f = fract(g);
  vec2 d = min(f, 1.0 - f) * size;
  return 1.0 - smoothstep(w * 0.5, w, min(d.x, d.y));
}
vec3 fsGround(float m, vec3 wp, float dist) {
  vec2 p = wp.xz;
  // size of one pixel on the ground: detail finer than that is faded out
  // instead of aliasing into moire patterns at grazing angles
  float fw = max(fwidth(p.x), fwidth(p.y));
  float fine = (1.0 - smoothstep(10.0, 45.0, dist)) * (1.0 - smoothstep(0.02, 0.07, fw));
  float mid = (1.0 - smoothstep(40.0, 220.0, dist)) * (1.0 - smoothstep(0.4, 1.6, fw));
  float patch_ = fsNoise(p * 0.31) - 0.5;
  float grain = (fsNoise(p * 17.0) - 0.5) * fine;
  float speck = (fsHash(floor(p * 55.0)) - 0.5) * fine * (1.0 - smoothstep(0.006, 0.02, fw));
  vec3 c;
  if (m < 0.5) {                       // asphalt
    c = vec3(0.325, 0.33, 0.345) * (1.0 + 0.16 * patch_ * mid) + grain * 0.06 + speck * 0.07;
  } else if (m < 1.5) {                // concrete slabs
    c = vec3(0.60, 0.60, 0.58) * (1.0 + 0.10 * patch_ * mid) + grain * 0.05;
    c *= 1.0 - 0.35 * fsJoints(p, vec2(5.0, 3.6), 0.0, 0.03) * mid;
  } else if (m < 2.5) {                // paving stones
    c = vec3(0.56, 0.54, 0.52) * (1.0 + 0.12 * patch_ * mid) + grain * 0.05;
    c *= 1.0 - 0.30 * fsJoints(p, vec2(0.2, 0.1), 0.5, 0.012) * fine;
    c *= 0.94 + 0.12 * fsHash(floor(p / vec2(0.2, 0.1))) * fine;
  } else if (m < 3.5) {                // setts / cobbles
    c = vec3(0.43, 0.42, 0.41) * (1.0 + 0.14 * patch_ * mid) + grain * 0.06;
    c *= 1.0 - 0.45 * fsJoints(p, vec2(0.13, 0.13), 0.5, 0.02) * fine;
    c *= 0.88 + 0.24 * fsHash(floor(p / 0.13)) * fine;
  } else if (m < 4.5) {                // gravel
    c = vec3(0.60, 0.57, 0.50) * (1.0 + 0.15 * patch_ * mid) + grain * 0.10 + speck * 0.12;
  } else if (m < 5.5) {                // dirt
    c = vec3(0.46, 0.39, 0.29) * (1.0 + 0.22 * patch_ * mid) + grain * 0.08 + speck * 0.05;
  } else if (m < 6.5) {                // grass track
    c = vec3(0.36, 0.47, 0.22) * (1.0 + 0.25 * patch_ * mid) + grain * 0.10;
  } else if (m < 7.5) {                // sidewalk slabs
    c = vec3(0.66, 0.65, 0.62) * (1.0 + 0.08 * patch_ * mid) + grain * 0.04;
    c *= 1.0 - 0.28 * fsJoints(p, vec2(0.5, 0.5), 0.5, 0.012) * fine;
  } else if (m < 8.5) {                // kerb stone
    c = vec3(0.72, 0.71, 0.69) + grain * 0.05;
    c *= 1.0 - 0.30 * fsJoints(p, vec2(1.0, 1.0), 0.0, 0.015) * fine;
  } else if (m < 9.5) {                // grass verge
    c = vec3(0.40, 0.52, 0.25) * (1.0 + 0.22 * patch_ * mid) + grain * 0.10 + speck * 0.05;
  } else if (m < 10.5) {               // worn asphalt
    c = vec3(0.40, 0.40, 0.40) * (1.0 + 0.2 * patch_ * mid) + grain * 0.07 + speck * 0.06;
  } else if (m < 11.5) {               // railway ballast
    c = vec3(0.44, 0.40, 0.37) + grain * 0.12 + speck * 0.14;
  } else if (m < 12.5) {               // footpath
    c = vec3(0.62, 0.59, 0.54) * (1.0 + 0.1 * patch_ * mid) + grain * 0.06;
  } else if (m < 13.5) {               // cycle path (red asphalt)
    c = vec3(0.56, 0.30, 0.26) * (1.0 + 0.12 * patch_ * mid) + grain * 0.05;
  } else if (m < 20.5) {               // white paint
    c = vec3(0.93, 0.93, 0.91) * (0.9 + 0.1 * fsNoise(p * 6.0)) + grain * 0.06;
  } else if (m < 21.5) {               // yellow paint
    c = vec3(0.95, 0.78, 0.12) + grain * 0.05;
  } else if (m < 22.5) {               // red paint
    c = vec3(0.70, 0.20, 0.16) + grain * 0.05;
  } else {                             // blue paint
    c = vec3(0.15, 0.32, 0.70) + grain * 0.05;
  }
  return fsLin(clamp(c, 0.0, 1.0));
}
)GLSL";

const char* const FACADE_GLSL = R"GLSL(
// uv.xy = position on the wall in metres (x along the wall, y above its foot),
// uv2.xy = wall length and eave height.
vec3 fsFacade(float style, vec3 base, vec4 uvd, vec3 wp, float dist, out float glass) {
  glass = 0.0;
  float fine = 1.0 - smoothstep(25.0, 110.0, dist);
  float mid = 1.0 - smoothstep(120.0, 420.0, dist);
  float grain = (fsNoise(wp.xz * 9.0 + wp.y * 7.0) - 0.5) * 0.06 * fine;
  vec3 c = base + grain;
  if (style > 7.5) {
    // roofs: 8 tiles, 9 flat, 10 metal
    if (style < 8.5) {
      float row = fract(uvd.y / 0.33);
      float col = fract(uvd.x / 0.24 + 0.5 * floor(uvd.y / 0.33));
      c *= 0.86 + 0.14 * smoothstep(0.0, 0.25, row) * fine + (1.0 - fine) * 0.07;
      c *= 1.0 - 0.10 * step(col, 0.08) * fine;
    } else if (style < 9.5) {
      c *= 0.95 + 0.10 * fsNoise(wp.xz * 1.7);
    } else {
      c *= 0.92 + 0.08 * step(0.5, fract(uvd.x / 0.5)) * fine;
    }
    return c;
  }
  float L = max(uvd.z, 0.1);
  float H = max(uvd.w, 0.1);
  // plinth
  c *= 1.0 - 0.18 * (1.0 - smoothstep(0.35, 0.45, uvd.y)) * mid;
  if (style < 0.5 || (style > 1.5 && style < 4.5)) {
    // regular window grid that always fits the wall
    float floorH = style < 0.5 ? 2.75 : 3.3;
    float bay = style < 0.5 ? 2.6 : (style < 2.5 ? 3.4 : 3.0);
    float rows = max(1.0, floor(H / floorH + 0.35));
    float cols = max(1.0, floor(L / bay + 0.3));
    if (L > 1.7 && H > 2.1) {
      vec2 cell = vec2(uvd.x / L * cols, uvd.y / H * rows);
      vec2 id = floor(cell);
      vec2 f = fract(cell);
      vec2 cs = vec2(L / cols, H / rows);          // cell size in metres
      vec2 win = style < 0.5 ? vec2(1.15, 1.30) : (style < 2.5 ? vec2(cs.x * 0.8, 1.6) : vec2(1.3, 1.7));
      win = min(win, cs * vec2(0.75, 0.62));
      vec2 lo = (cs - win) * vec2(0.5, 0.42);
      vec2 q = f * cs;
      float inside = step(lo.x, q.x) * step(q.x, lo.x + win.x) * step(lo.y, q.y) * step(q.y, lo.y + win.y);
      float below = step(uvd.y, H);                // no windows in the gable triangle
      float rnd = fsHash(id + floor(wp.xz * 0.05));
      // a door on the ground floor now and then
      float door = step(id.y, 0.5) * step(0.82, rnd) * step(abs(q.x - cs.x * 0.5), 0.5) * step(q.y, 2.05) * (style < 0.5 ? 1.0 : 0.0);
      float w = inside * below * mid * (1.0 - door);
      // frame
      vec2 e = min(q - lo, lo + win - q);
      float frame = (1.0 - smoothstep(0.03, 0.07, min(e.x, e.y))) * w * fine;
      float mullion = (1.0 - smoothstep(0.015, 0.035, abs(q.x - (lo.x + win.x * 0.5)))) * w * fine;
      vec3 glassCol = mix(vec3(0.10, 0.14, 0.19), vec3(0.32, 0.42, 0.52), 0.35 + 0.5 * rnd);
      c = mix(c, glassCol, w);
      c = mix(c, vec3(0.88, 0.88, 0.86), max(frame, mullion * 0.8));
      c = mix(c, base * 0.45 + vec3(0.12, 0.09, 0.06), door * mid);
      glass = w * (1.0 - max(frame, mullion));
    }
  } else if (style < 1.5) {
    // garage: one sectional door on walls wide enough
    if (L > 2.4 && L < 7.5 && H > 2.0) {
      float cols = max(1.0, floor(L / 3.0));
      float cw = L / cols;
      float qx = fract(uvd.x / cw) * cw;
      float door = step(abs(qx - cw * 0.5), min(1.2, cw * 0.4)) * step(uvd.y, 2.05) * step(0.5, fsHash(floor(wp.xz * 0.21)));
      float ribs = 0.92 + 0.08 * step(0.5, fract(uvd.y / 0.5));
      c = mix(c, vec3(0.78, 0.78, 0.76) * ribs, door * mid);
    }
  }
  return c;
}
)GLSL";

// ---------------------------------------------------------------------------
// The three.js `#include` plumbing, spelled out for WebGL2
// ---------------------------------------------------------------------------

const char* const WORLDPOS_VS_HEAD =
    "varying vec3 vFsWorld;\n";

const char* const WORLDPOS_VS_BODY =
    "  vFsWorld = (modelMatrix * vec4(transformed, 1.0)).xyz;\n";

const char* const WORLDPOS_FS_HEAD =
    "varying vec3 vFsWorld;\n";

// ---------------------------------------------------------------------------
// Shader assembly
// ---------------------------------------------------------------------------

namespace {

const char* const VS_HEAD = R"GLSL(
precision highp float;
attribute vec3 position;
attribute vec3 normal;
attribute vec2 uv;
uniform mat4 modelMatrix;
uniform mat4 viewMatrix;
uniform mat4 projectionMatrix;
uniform mat4 modelViewMatrix;
uniform mat3 normalMatrix;
uniform vec3 cameraPosition;
)GLSL";

const char* const VS_MAIN = R"GLSL(
void main() {
  vec3 transformed = position;
  vec3 objectNormal = normal;
)GLSL";

const char* const VS_TAIL = R"GLSL(
  vec4 mvPosition = modelViewMatrix * vec4(transformed, 1.0);
  gl_Position = projectionMatrix * mvPosition;
}
)GLSL";

const char* const FS_HEAD = R"GLSL(
precision highp float;
uniform mat4 viewMatrix;
uniform vec3 cameraPosition;
uniform vec3 diffuse;
uniform vec3 ambient;
uniform vec3 emissive;
uniform vec3 fogColor;
uniform float fogNear;
uniform float fogFar;
uniform vec3 uSunDir;
uniform vec3 uSunCol;
uniform float uSunInt;
uniform vec3 uSkyCol;
uniform vec3 uGroundCol;
uniform float uHemiInt;
uniform vec3 uZenith;
uniform vec3 uHorizon;
uniform vec3 uSunColor;
uniform sampler2D map;
uniform vec2 uTexel;
)GLSL";

const char* const FS_TAIL = R"GLSL(
void main() {
  vec3 outgoingLight = vec3(0.0);
  vec4 diffuseColor = vec4(diffuse, 1.0);
)GLSL";

const char* const FS_END = R"GLSL(
  // three.js fog: mix towards the fog colour with distance
  float fogDepth = gl_FragCoord.z / gl_FragCoord.w;
  float fogFactor = smoothstep(fogNear, fogFar, fogDepth);
  outgoingLight = mix(outgoingLight, fogColor, fogFactor);
  gl_FragColor = vec4(outgoingLight, diffuseColor.a);
}
)GLSL";

}  // namespace

std::string vertexShaderFor(MaterialKind kind) {
  std::string s = VS_HEAD;
  if (kind == MaterialKind::Ground || kind == MaterialKind::Paint) {
    s += "attribute float amat;\nflat varying float vFsMat;\n";
  }
  if (kind == MaterialKind::Building) {
    s += "attribute vec4 astyle;\nattribute vec4 auvd;\nflat varying vec4 vFsStyle;\nvarying vec4 vFsUvd;\n";
  }
  if (kind != MaterialKind::Sky) s += WORLDPOS_VS_HEAD;
  if (kind != MaterialKind::Sky) s += "varying vec3 vNormal;\n";
  s += VS_MAIN;
  if (kind == MaterialKind::Ground || kind == MaterialKind::Paint) s += "  vFsMat = amat;\n";
  if (kind == MaterialKind::Building) s += "  vFsStyle = astyle;\n  vFsUvd = auvd * 0.02;\n";
  if (kind != MaterialKind::Sky) {
    s += WORLDPOS_VS_BODY;
    s += "  vNormal = normalMatrix * objectNormal;\n";
  }
  s += VS_TAIL;
  return s;
}

std::string fragmentShaderFor(MaterialKind kind) {
  std::string s = FS_HEAD;
  if (kind == MaterialKind::Sky) {
    // the sky dome: three.js ShaderMaterial, no lighting
    s = R"GLSL(
precision highp float;
varying vec3 vDir;
uniform vec3 uSunDir;
uniform vec3 uZenith;
uniform vec3 uHorizon;
uniform vec3 uSunColor;
void main() {
  vec3 d = normalize(vDir);
  float h = clamp(d.y, -0.2, 1.0);
  float t = pow(1.0 - max(h, 0.0), 3.2);
  vec3 col = mix(uZenith, uHorizon, t);
  // below the horizon: fade to the fog colour so distant terrain blends in
  col = mix(col, uHorizon, smoothstep(0.0, -0.08, d.y));
  float s = max(dot(d, uSunDir), 0.0);
  col += uSunColor * (pow(s, 900.0) * 6.0 + pow(s, 24.0) * 0.22 + pow(s, 4.0) * 0.06);
  gl_FragColor = vec4(col, 1.0);
}
)GLSL";
    return s;
  }
  s += WORLDPOS_FS_HEAD;
  s += "varying vec3 vNormal;\n";
  if (kind == MaterialKind::Ground || kind == MaterialKind::Paint)
    s += "flat varying float vFsMat;\n";
  if (kind == MaterialKind::Building)
    s += "flat varying vec4 vFsStyle;\nvarying vec4 vFsUvd;\n";
  s += NOISE_GLSL;
  if (kind == MaterialKind::Ground || kind == MaterialKind::Paint) s += GROUND_GLSL;
  if (kind == MaterialKind::Building) s += FACADE_GLSL;
  s += FS_TAIL;

  // lambert lighting shared by the lit materials (three.js MeshLambertMaterial):
  // one directional sun + a hemisphere ambient, both supplied by the renderer.
  auto lambert = []() {
    return std::string(
        "  vec3 N = normalize(vNormal);\n"
        "  float ndl = max(dot(N, uSunDir), 0.0);\n"
        "  float hemiMix = 0.5 + 0.5 * N.y;\n"
        "  vec3 hemi = mix(uGroundCol, uSkyCol, hemiMix) * uHemiInt;\n"
        "  outgoingLight = diffuseColor.rgb * (hemi + uSunCol * uSunInt * ndl);\n");
  };

  switch (kind) {
    case MaterialKind::SignBack:
    case MaterialKind::Struct:
      // the plate's back: the atlas silhouette in bare metal (the original
      // overrides `diffuseColor.rgb` after <map_fragment>)
      s += "  diffuseColor.rgb = vec3(0.30, 0.32, 0.33);\n";
      s += lambert();
      break;
    case MaterialKind::Ground:
    case MaterialKind::Paint:
      s += "  diffuseColor.rgb = fsGround(vFsMat, vFsWorld, distance(vFsWorld, cameraPosition));\n";
      s += lambert();
      break;
    case MaterialKind::Terrain: {
      s += "  {\n";
      s += "    float d = distance(vFsWorld, cameraPosition);\n";
      s += "    float fine = 1.0 - smoothstep(8.0, 50.0, d);\n";
      s += "    float mid = 1.0 - smoothstep(60.0, 400.0, d);\n";
      s += "    float n = (fsNoise(vFsWorld.xz * 0.11) - 0.5) * 0.30 * (0.35 + 0.65 * mid)\n";
      s += "            + (fsNoise(vFsWorld.xz * 1.3) - 0.5) * 0.22 * mid\n";
      s += "            + (fsNoise(vFsWorld.xz * 19.0) - 0.5) * 0.30 * fine;\n";
      s += "    diffuseColor.rgb *= 1.0 + n;\n";
      s += "  }\n";
      s += lambert();
      break;
    }
    case MaterialKind::Building: {
      s += "  float fsGlass;\n";
      s += "  vec3 fsBase = vFsStyle.yzw / 255.0;\n";
      s += "  diffuseColor.rgb = fsLin(clamp(fsFacade(vFsStyle.x, fsBase, vFsUvd, vFsWorld, distance(vFsWorld, cameraPosition), fsGlass), 0.0, 1.0));\n";
      s += lambert();
      break;
    }
    case MaterialKind::Sky:
      break;
  }
  s += FS_END;
  return s;
}

std::string programCacheKey(MaterialKind kind, bool signBack) {
  if (signBack) return "fs-sign-back";
  return programCacheKeyFor(kind);
}

std::string programCacheKeyFor(MaterialKind kind) {
  switch (kind) {
    case MaterialKind::SignBack: return "fs-sign-back";
    case MaterialKind::Ground: return "fs-ground";
    case MaterialKind::Paint: return "fs-paint";
    case MaterialKind::Terrain: return "fs-terrain";
    case MaterialKind::Building: return "fs-bldg";
    case MaterialKind::Struct: return "fs-struct";
    case MaterialKind::Sky: return "fs-sky";
  }
  return "fs-unknown";
}

}  // namespace fs
