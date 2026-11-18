// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Constants of the road-network model (class names, priority tiers, lane
 * widths, surface tables).
 *
 * Origin: src/world/types.ts (MaurerAnton/driving-simulator @ 5b6ddc7, v0.1.0).
 */
#include "world/types.h"

namespace fs {

const char* const ROAD_CLASSES[15] = {
    "motorway", "motorway_link", "trunk", "trunk_link", "primary",
    "primary_link", "secondary", "secondary_link", "tertiary",
    "tertiary_link", "unclassified", "residential", "living_street",
    "service", "track",
};

const double CLASS_TIER[15] = {7, 2.5, 6, 2.5, 5, 4.5, 4, 3.5, 3, 2.5, 2, 2, 1, 0, 0};

const double CLASS_LANE_WIDTH[15] = {
    3.75, 3.75, 3.5, 3.5, 3.5, 3.5, 3.25, 3.5, 3.1, 3.4, 2.85, 2.75, 2.6, 2.6, 2.4,
};

const double SURF_GRIP[7] = {1.0, 0.97, 0.92, 0.82, 0.62, 0.52, 0.42};

const double SURF_ROLLING[7] = {1.0, 1.0, 1.25, 1.6, 2.2, 3.0, 4.0};

}  // namespace fs
