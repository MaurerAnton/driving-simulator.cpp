// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/loading.ts @ v1.7.7 (the pallets: PalletLoad, PALLET, palletModel – the forklift's place draws them;
// the depot's loading scene – LoadingScene, its forklift, pallet truck and warehouse – comes with the services, game/serviceworld)
#pragma once

#include <memory>

#include "engine/engine.h"

namespace fs {

/** A pallet to be loaded: how tall its goods stand, and what they are (0 cartons, 1 sacks, 2 drums, 3 crates). */
struct PalletLoad {
  double height = 0;
  int kind = 0;
  double mass = 0;
};

/** Euro pallet: 1.2 m along the truck, 0.8 m across, 0.144 m high. */
struct PalletSize {
  double len, wid, base;
};
inline constexpr PalletSize PALLET = {1.2, 0.8, 0.144};

/** A pallet with its goods (its middle on the ground at the origin, the long side along z) – also the forklift's (render/forkliftyard.h). */
std::shared_ptr<Group> palletModel(const PalletLoad& p);

}  // namespace fs
