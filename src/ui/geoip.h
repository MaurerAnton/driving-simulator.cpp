// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/geoip.ts @ v1.7.7
/**
 * Where the player is, from their IP address alone (ui/geoip.ts).
 *
 * On the first start the game may load the region the player is in – but asking
 * the browser for the position means a permission prompt, and a game that opens
 * with "may we know where you are?" is a game that gets closed.  The IP says it
 * instead: a service that answers where an address is, without asking anyone.
 *
 * It is not as exact as the GPS (a city at best, and a wrong guess now and then),
 * which is enough: a region is a land of tens of kilometres, not a street.
 *
 * `ipPlace()` asks ipwho.is (free, no key, CORS open); `ipRegion()` turns the
 * answer into the region whose box holds it (world/regions.ts).  Both fail
 * quietly – no region is chosen, the site's own map loads as always.
 */
#pragma once

#include <functional>
#include <optional>

#include "core/task.h"
#include "platform/plat.h"
#include "world/regions.h"

namespace fs {

/** `typeof fetch`: what asks the network (tests bring their own; an empty one: plat::fetch) */
using FetchFn = std::function<Task<plat::Response>(plat::Request request)>;

struct LatLon {
  double lat = 0;
  double lon = 0;
  bool operator==(const LatLon&) const = default;
};

/** Where an IP address is: its latitude and longitude, or why not. */
using IpPlace = std::optional<LatLon>;

/**
 * Where this browser's IP address is, from ipwho.is – null when the service is
 * unreachable, refuses, or gives no coordinates.  Never throws: the caller goes
 * on without a place.
 */
Task<IpPlace> ipPlace(FetchFn fetchFn = nullptr);

/**
 * The region the IP address lies in, or null (the place is in none of them, or
 * there was no place).  The index is the regions of map-assets (regions.json).
 */
const Region* ipRegion(const RegionIndex* index, const IpPlace& place);

}  // namespace fs
