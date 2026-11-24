// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/world/roadfeatures.ts @ v1.7.7
#include "world/roadfeatures.h"

namespace fs {

int calmKind(const std::string& v) {
  if (v == "table" || v == "raised_table") return CALM_TABLE;
  if (v == "hump") return CALM_HUMP;
  if (v == "bump") return CALM_BUMP;
  if (v == "cushion") return CALM_CUSHION;
  return 0;
}

}  // namespace fs
