// SPDX-License-Identifier: AGPL-3.0-only
// LINK: sim world core platform geo
/**
 * What the simulation asks of the map layer that the map layer it was built against does not
 * have (src/sim/world_needs.h): one line each.  Nothing printed but the last line: the two
 * layers are joined, and the stand-ins can go.
 *
 *   tools/t.sh sim_needs
 */
#include <cstdio>

#include "src/sim/world_needs.h"

int main() {
  const std::vector<const char*> missing = fs::needs::missing();
  for (const char* m : missing) std::printf("needs from world: %s\n", m);
  std::printf("%zu of the simulation's needs are met by stand-ins (src/sim/world_needs.h)\n", missing.size());
  return 0;
}
