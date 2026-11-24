// SPDX-License-Identifier: AGPL-3.0-only
// Writes src/engine/three_glsl.cpp: the GLSL of three.js that the port's renderer compiles
// (the shaders of MeshBasic/Lambert/Phong/Depth/Sprite/Points materials and the chunks they
// include), so that the picture is the original's and its shader patches still fit.
//
//   node tools/gen-three-glsl.mjs [path of the original repository]
//
// Run again when the original moves to another three.js; the result is committed.
import { readFileSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const original = resolve(process.argv[2] ?? '/home/user/driving-simulator');
const three = join(original, 'node_modules/three');
const shaders = join(three, 'src/renderers/shaders');
const version = JSON.parse(readFileSync(join(three, 'package.json'), 'utf8')).version;

const LIBS = ['meshbasic', 'meshlambert', 'meshphong', 'depth', 'sprite', 'points'];
const libs = {};
for (const name of LIBS) {
  const m = await import(pathToFileURL(join(shaders, 'ShaderLib', `${name}.glsl.js`)).href);
  libs[name] = { vertex: m.vertex, fragment: m.fragment };
}
const chunks = {};
const todo = [];
const want = (src) => {
  for (const m of src.matchAll(/^[ \t]*#include +<([\w\d./]+)>/gm)) if (!(m[1] in chunks)) {
    chunks[m[1]] = null;
    todo.push(m[1]);
  }
};
for (const l of Object.values(libs)) {
  want(l.vertex);
  want(l.fragment);
}
// (what the prefix of a fragment shader is made of: see WebGLProgram.js)
for (const extra of ['tonemapping_pars_fragment', 'colorspace_pars_fragment']) if (!(extra in chunks)) {
  chunks[extra] = null;
  todo.push(extra);
}
while (todo.length) {
  const name = todo.pop();
  const m = await import(pathToFileURL(join(shaders, 'ShaderChunk', `${name}.glsl.js`)).href);
  chunks[name] = m.default;
  want(m.default);
}

const raw = (s) => {
  if (s.includes(')GLSL"')) throw new Error('the delimiter occurs in a shader');
  // (a string literal may not be longer than some compilers take: cut into pieces of a few kB at line ends)
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

let out = `// SPDX-License-Identifier: MIT
/**
 * The shaders of three.js r${version} that the renderer compiles – written by
 * tools/gen-three-glsl.mjs, do not edit.
 *
 * three.js is Copyright © 2010-2026 three.js authors, MIT licence (see NOTICE.md).
 */
#include "engine/three_glsl.h"

namespace fs::three {

const char* const REVISION = "${version}";

const Chunk CHUNKS[] = {
`;
const names = Object.keys(chunks).sort();
for (const n of names) out += `{"${n}",\n${raw(chunks[n])}},\n`;
out += `};
const int CHUNK_COUNT = ${names.length};

const Lib LIBS[] = {
`;
for (const n of LIBS) out += `{"${n}",\n${raw(libs[n].vertex)},\n${raw(libs[n].fragment)}},\n`;
out += `};
const int LIB_COUNT = ${LIBS.length};

}  // namespace fs::three
`;
const target = join(here, '../src/engine/three_glsl.cpp');
writeFileSync(target, out);
console.log(`three.js ${version}: ${names.length} chunks, ${LIBS.length} shaders, ${out.length} bytes → ${target}`);
