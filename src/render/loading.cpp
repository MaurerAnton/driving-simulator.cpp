// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/loading.ts @ v1.7.7 (the pallets – see loading.h)
#include "render/loading.h"

#include <array>

#include "engine/geometries.h"

namespace fs {

namespace {

/** (the original's module constants that the pallets use: made when first asked for, kept to the end) */
struct LoadingMaterials {
  const MaterialPtr wood = lambert({.color = 0xb08a5a});
  const std::array<MaterialPtr, 4> GOODS = {lambert({.color = 0xb98c5a}), lambert({.color = 0xe8e4d8}), lambert({.color = 0x2f62c4}), lambert({.color = 0x9a7b4f})};
};

const LoadingMaterials& MAT() {
  static const LoadingMaterials* m = new LoadingMaterials();
  return *m;
}

std::shared_ptr<Mesh> boxMesh(double w, double h, double d, double x, double y, double z, const MaterialPtr& m) {
  auto mesh = make<Mesh>(boxGeometry(w, h, d), m);
  mesh->position.set(x, y + h / 2, z);
  mesh->castShadow = true;
  mesh->receiveShadow = true;
  return mesh;
}

}  // namespace

std::shared_ptr<Group> palletModel(const PalletLoad& p) {
  auto g = make<Group>();
  // the pallet: three runners and the deck
  for (double x : {-0.34, 0.0, 0.34}) g->add(boxMesh(0.1, 0.1, PALLET.len, x, 0, 0, MAT().wood));
  g->add(boxMesh(PALLET.wid, 0.044, PALLET.len, 0, 0.1, 0, MAT().wood));
  const double h = p.height;
  const MaterialPtr& m = MAT().GOODS[size_t(p.kind) % MAT().GOODS.size()];
  if (p.kind == 2) {
    // drums
    for (const auto& [x, z] : {std::pair{-0.2, -0.3}, std::pair{0.2, -0.3}, std::pair{-0.2, 0.3}, std::pair{0.2, 0.3}}) {
      auto d = make<Mesh>(cylinderGeometry(0.19, 0.19, h, 12), m);
      d->position.set(x, PALLET.base + h / 2, z);
      d->castShadow = true;
      g->add(d);
    }
  } else
    g->add(boxMesh(PALLET.wid - 0.04, h, PALLET.len - 0.04, 0, PALLET.base, 0, m));
  return g;
}

}  // namespace fs
