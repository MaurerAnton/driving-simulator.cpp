// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/bigmap.ts @ v1.7.7 (draw(): the aerial photographs under the roads)
/**
 * 2.0: the big map's aerial layer, kept apart from the rest of the map: the states'
 * orthophotos and Sentinel-2 for whatever the map shows – the loaded region, any other
 * region's land, all of Germany (reality/aerial.h drawAerial).  The map's world frame
 * is the same for every region, so a tile lies on the roads wherever it is.
 */
#include <string>

#include "reality/aerial.h"
#include "ui/bigmap.h"
#include "world/world.h"

namespace fs {

void BigMap::drawAerialLayer(const Ctx2D& g, double W, double H, double dpr) {
  if (!photos) return;
  const WorldMetaJson& m = host.world->json.meta;
  AerialView v;
  v.cx = cx;
  v.cz = cz;
  v.scale = scale;
  v.width = W;
  v.height = H;
  v.dpr = dpr;
  v.originE = m.originE;
  v.originN = m.originN;
  const std::vector<std::string> credits = drawAerial(g, *photos, v);
  aerialCredit.clear();
  for (const std::string& c : credits) aerialCredit += (aerialCredit.empty() ? "" : " · ") + c;
}

}  // namespace fs
