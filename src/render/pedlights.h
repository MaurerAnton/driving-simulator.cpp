// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The pedestrian lights (world/pedlights says where they stand): a head
 * with the red standing and the green walking figure, facing across the
 * road, and the yellow box with the button and the lamp "Signal kommt".
 * Everything instanced – a town has hundreds of them.
 *
 * Origin: src/render/pedlights.ts @ v1.7.7
 */
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "engine/engine.h"
#include "world/pedlights.h"

namespace fs {

class SignalSystem;

class PedLightRenderer {
 public:
  const std::shared_ptr<Group> group = make<Group>();

  PedLightRenderer();

  /** Set the lights up anew (the road network comes in pieces). */
  void rebuild(const std::vector<PedPole>& poles);
  void update(SignalSystem& signals);
  void dispose();

 private:
  std::vector<PedPole> poles_;
  std::vector<std::shared_ptr<InstancedMesh>> meshes_;
  std::shared_ptr<InstancedMesh> red_;
  std::shared_ptr<InstancedMesh> green_;
  std::shared_ptr<InstancedMesh> lamp_;
  /** what each pole shows now: 0 red, 1 green, 2 dark (−1: not set yet); the request lamp */
  std::vector<int8_t> shown_;
  std::vector<int8_t> asked_;
  const TexturePtr figureTex_;
  const TexturePtr requestTex_;
  // (in front of the face of the head and of the box, also from afar: drawn nearer than the faces they lie on)
  const MaterialPtr figureMat_;
  const MaterialPtr requestMat_;
  const MaterialPtr housingMat_;
  const MaterialPtr boxMat_;
};

}  // namespace fs
