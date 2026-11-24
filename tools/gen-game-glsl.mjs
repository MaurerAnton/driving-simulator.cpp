// SPDX-License-Identifier: AGPL-3.0-only
// Writes src/render/glsl_gen.cpp: the game's own shaders, taken from the original by running
// its material code – each material is made as the original makes it, its onBeforeCompile is
// run on three.js's shader for its kind, and what comes out is kept, with the cache key and the
// names of the uniforms the hook added.  Nothing is typed over by hand.
//
// Which materials: every file tools/glsl/<part>.mjs lists some (one file per part of the game, so
// that two people adding shaders do not edit the same file).  In C++: `gamePatch("name")`
// (src/render/glsl_gen.h) is the patch that puts the captured shader on a material.
//
//   node tools/gen-game-glsl.mjs [path of the original repository]
//
// Run again when the original's shaders change; the result is committed.
import { readdirSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const original = resolve(process.argv[2] ?? '/home/user/driving-simulator');
const load = (p) => import(pathToFileURL(join(original, p)).href);
const THREE = await load('node_modules/three/build/three.module.js');

/** three.js's shader for a material's kind */
function libOf(m) {
  if (m.isMeshLambertMaterial) return THREE.ShaderLib.lambert;
  if (m.isMeshBasicMaterial || m.isLineBasicMaterial) return THREE.ShaderLib.basic;
  if (m.isMeshPhongMaterial) return THREE.ShaderLib.phong;
  if (m.isPointsMaterial) return THREE.ShaderLib.points;
  if (m.isSpriteMaterial) return THREE.ShaderLib.sprite;
  throw new Error(`no shader for ${m.type}`);
}

/** Run a material's hook the way the renderer would; what it leaves. */
function capture(name, material) {
  const lib = libOf(material);
  const shader = { vertexShader: lib.vertexShader, fragmentShader: lib.fragmentShader, uniforms: {}, defines: {} };
  const before = new Set(Object.keys(shader.uniforms));
  material.onBeforeCompile(shader, null);
  return {
    name,
    key: material.customProgramCacheKey(),
    vertex: shader.vertexShader,
    fragment: shader.fragmentShader,
    uniforms: Object.keys(shader.uniforms).filter((u) => !before.has(u)),
  };
}

// every tools/glsl/*.mjs names materials of the original: `export async function captures({ load, THREE })`
// returns [[name, material], …] (see tools/glsl/tiles.mjs)
const out = [];
const dir = join(here, 'glsl');
for (const file of readdirSync(dir).filter((f) => f.endsWith('.mjs')).sort()) {
  const mod = await import(pathToFileURL(join(dir, file)).href);
  for (const [name, material] of await mod.captures({ load, THREE })) {
    if (out.some((o) => o.name === name)) throw new Error(`two shaders are called ${name}`);
    out.push(capture(name, material));
  }
}

const raw = (s) => {
  if (s.includes(')GLSL"')) throw new Error('the delimiter occurs in a shader');
  const parts = [];
  let cur = '';
  for (const line of s.split('\n')) {
    if (cur.length + line.length > 6000) {
      parts.push(cur);
      cur = '';
    }
    cur += line + '\n';
  }
  parts.push(cur.replace(/\n$/, ''));
  return parts.map((p) => `R"GLSL(${p})GLSL"`).join('\n');
};

let cpp = `// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The game's own shaders, as the original's materials make them – written by
 * tools/gen-game-glsl.mjs from the original's code, do not edit.
 */
#include "render/glsl_gen.h"

#include <cstring>

namespace fs {

const GameShader GAME_SHADERS[] = {
`;
for (const s of out) {
  cpp += `{"${s.name}", "${s.key}",\n${raw(s.vertex)},\n${raw(s.fragment)},\n"${s.uniforms.join(' ')}"},\n`;
}
cpp += `};
const int GAME_SHADER_COUNT = ${out.length};

const GameShader* gameShader(const char* name) {
  for (int i = 0; i < GAME_SHADER_COUNT; i++)
    if (!std::strcmp(GAME_SHADERS[i].name, name)) return &GAME_SHADERS[i];
  return nullptr;
}

}  // namespace fs
`;
writeFileSync(join(here, '../src/render/glsl_gen.cpp'), cpp);
console.log(out.map((s) => `${s.name} (${s.key}; ${s.vertex.length + s.fragment.length} bytes${s.uniforms.length ? '; uniforms ' + s.uniforms.join(', ') : ''})`).join('\n'));
