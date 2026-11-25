// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/world/levelxing.ts @ v1.7.7
/**
 * What a level crossing (Bahnübergang) is equipped with: barriers (half or
 * full), lights (the yellow and red "Lichtzeichen", or the older red
 * flashing "Blinklicht"), a bell – or nothing but the St Andrew's cross
 * (Zeichen 201), where the trains have the right of way and the road
 * traffic looks out for them (§ 19 Abs. 1, 2 StVO).
 *
 * The map knows where a road crosses a track (CrossingJson of kind
 * XING.RAIL or XING.TRAM), not how the crossing is secured; that comes from
 * the tags of OpenStreetMap kept in levelcrossings.data.h (written by
 * tools/build-levelcrossings.ts) and matched by position.  Where the map
 * does not say, it is guessed from the road: barriers and lights on the
 * main roads, lights on the smaller ones, only the cross on tracks and
 * service roads – and a crossing of the trams has nothing of its own
 * (unless the map says so): the junction's lights, or the tram's right of
 * way, rule it.
 */
#pragma once

#include <optional>
#include <string>

namespace fs {

/** 'none' | 'half' | 'full' */
using Barrier = std::string;
/** the yellow and then red lights of today, the red flashing light of older crossings, or none: 'none' | 'yellowRed' | 'blink' */
using LightKind = std::string;

struct Equipment {
  Barrier barrier = "none";
  LightKind light = "none";
  bool bell = false;
  /** a crossing of a railway (else of a tramway) */
  bool rail = false;
  /** whether the tags of the map said what it has (else it is guessed from the road) */
  bool mapped = false;
};

/** What the map says of a crossing (letters as in levelcrossings.data.h). */
struct XingTags {
  char barrier = '-';
  char light = '-';
  char bell = '-';
  char control = '-';
};

/** The tags of the map for the crossing at (x, z): the nearest record within `reach` metres, or none. */
std::optional<XingTags> taggedAt(double x, double z, double reach = 8);

/**
 * What the crossing at (x, z) is equipped with: a crossing of a railway
 * (`rail`) or of a tramway, on a road of priority tier `tier`
 * (CLASS_TIER) – the map's tags where it has them, else a guess.
 */
Equipment equipmentFor(double x, double z, bool rail, double tier, const std::optional<XingTags>& tags);
inline Equipment equipmentFor(double x, double z, bool rail, double tier) { return equipmentFor(x, z, rail, tier, taggedAt(x, z)); }

}  // namespace fs
