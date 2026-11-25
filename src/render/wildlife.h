// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/wildlife.ts @ v1.7.7
/**
 * The deer and wild boar that cross the country roads at dusk
 * (sim/wildlife.ts), drawn with the models of render/animals.ts – a few at
 * a time, the models kept for the next group.
 */
#pragma once

#include <map>
#include <memory>
#include <vector>

#include "engine/engine.h"
#include "render/animals.h"
#include "sim/wildlife.h"

namespace fs {

class WildView {
 public:
  const std::shared_ptr<Group> group = make<Group>();

  WildView();
  WildView(const WildView&) = delete;
  WildView& operator=(const WildView&) = delete;

  /** Show exactly these animals (each frame). */
  void update(const std::vector<WildAnimal>& animals);

 private:
  std::vector<std::unique_ptr<AnimalModel>> shown;
  std::map<AnimalKind, std::vector<std::unique_ptr<AnimalModel>>> spare;
};

}  // namespace fs
