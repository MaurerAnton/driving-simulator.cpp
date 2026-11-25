// SPDX-License-Identifier: AGPL-3.0-only
/**
 * What an object of the engine held on the graphics card, handed back when the
 * object goes: its destructor names the handle here, the renderer frees it at the
 * start of the next frame (a destructor may run where no context is current).
 */
#pragma once

#include <cstdint>
#include <vector>

namespace fs::gpu {

enum class Kind { Buffer, VertexArray, Texture, Framebuffer, Renderbuffer, Programs };

struct Freed {
  Kind kind;
  uint32_t handle;
  void* pointer;
};

/** the handles waiting to be freed (the renderer empties it) */
inline std::vector<Freed>& freed() {
  // (never destroyed: a material or a geometry held by a static object is let go while the program ends, after a
  // static list would be gone already – that wrote into freed memory, crane_play_test's "double free")
  static auto* list = new std::vector<Freed>();
  return *list;
}

/** hand a handle back (0 is ignored) */
inline void release(Kind kind, uint32_t handle) {
  if (handle) freed().push_back(Freed{kind, handle, nullptr});
}
/** hand back what a material's `gpuPrograms` points to (null is ignored) */
inline void releasePrograms(void* pointer) {
  if (pointer) freed().push_back(Freed{Kind::Programs, 0, pointer});
}

/** the next id for a geometry, a material, a texture, an object */
inline uint32_t nextId() {
  static uint32_t id = 0;
  return ++id;
}

}  // namespace fs::gpu
