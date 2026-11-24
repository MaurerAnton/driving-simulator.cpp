// SPDX-License-Identifier: AGPL-3.0-only
/** The GLSL of three.js the renderer compiles (src/engine/three_glsl.cpp, written by tools/gen-three-glsl.mjs). */
#pragma once

namespace fs::three {

struct Chunk {
  const char* name;
  const char* source;
};
struct Lib {
  const char* name;
  const char* vertex;
  const char* fragment;
};

extern const char* const REVISION;
extern const Chunk CHUNKS[];
extern const int CHUNK_COUNT;
extern const Lib LIBS[];
extern const int LIB_COUNT;

}  // namespace fs::three
