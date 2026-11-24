// SPDX-License-Identifier: AGPL-3.0-only
/** The game's own shaders (src/render/glsl_gen.cpp, written by tools/gen-game-glsl.mjs from the original). */
#pragma once

#include "engine/engine.h"

namespace fs {

struct GameShader {
  const char* name;
  /** the original's customProgramCacheKey */
  const char* key;
  /** the whole shaders as the original's onBeforeCompile leaves them (with their #include lines) */
  const char* vertex;
  const char* fragment;
  /** the uniforms the hook adds, separated by spaces (their values are the material's to give) */
  const char* uniforms;
};

extern const GameShader GAME_SHADERS[];
extern const int GAME_SHADER_COUNT;

/** the shader of that name (null: there is none) */
const GameShader* gameShader(const char* name);

/** A patch that puts the shader in place of three.js's (null if there is no shader of that name). */
inline ShaderPatchPtr gamePatch(const char* name) {
  const GameShader* s = gameShader(name);
  if (!s) return nullptr;
  auto p = std::make_shared<ShaderPatch>();
  p->key = s->key;
  p->vertexSource = s->vertex;
  p->fragmentSource = s->fragment;
  return p;
}

}  // namespace fs
