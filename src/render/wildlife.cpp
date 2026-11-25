// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/wildlife.ts @ v1.7.7
#include "render/wildlife.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace fs {

WildView::WildView() { group->name = "wildlife"; }

void WildView::update(const std::vector<WildAnimal>& animals) {
  // (the models in the order of the animals: kept while the same kinds come)
  const size_t n = std::max(animals.size(), shown.size());
  if (shown.size() < n) shown.resize(n);
  for (size_t i = 0; i < n; i++) {
    const WildAnimal* a = i < animals.size() ? &animals[i] : nullptr;
    std::unique_ptr<AnimalModel>& m = shown[i];
    if (m && (!a || m->kind != a->kind)) {
      group->remove(m->group);
      spare[m->kind].push_back(std::move(m));
    }
    if (!a) continue;
    if (!m) {
      std::vector<std::unique_ptr<AnimalModel>>& list = spare[a->kind];
      if (!list.empty()) {
        m = std::move(list.back());
        list.pop_back();
      } else m = std::make_unique<AnimalModel>(a->kind);
      group->add(m->group);
    }
    const bool known = !std::isnan(a->y);
    m->group->visible = known;
    if (!known) continue;
    m->group->position.set(a->x, a->y, a->z);
    m->group->rotation.y = a->yaw;
    m->pose(a->dist, a->speed);
  }
  shown.resize(animals.size());
}

}  // namespace fs
