// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The files of a map as their records (world/types.h), read from JSON and written to it:
 * what `JSON.parse` and `JSON.stringify` are for these in the original.  The records are
 * read straight from the text (platform/json.h: JsonReader) – a chunk of the road network
 * is megabytes of numbers and arrives while the game runs.
 *
 *   WorldChunkJson chunk;
 *   if (!jsonParse(text, chunk)) …        // not JSON
 *   std::string text = jsonText(chunk);
 *
 * Origin: the shapes of src/world/types.ts and src/world/xsection.ts @ v1.7.7, and
 * `readJson` of src/world/loader.ts.
 */
#pragma once

#include <string_view>

#include "platform/json.h"
#include "world/types.h"

namespace fs {

FS_JSON_DECLARE(GridBits)
FS_JSON_DECLARE(RoadSideJson)
FS_JSON_DECLARE(YardJson)
FS_JSON_DECLARE(RailJson)
FS_JSON_DECLARE(StopsJson)
FS_JSON_DECLARE(RunwayJson)
FS_JSON_DECLARE(TaxiwayJson)
FS_JSON_DECLARE(ApronJson)
FS_JSON_DECLARE(AirfieldJson)
FS_JSON_DECLARE(AirJson)
FS_JSON_DECLARE(LineJson)
FS_JSON_DECLARE(LinesJson)
FS_JSON_DECLARE(BusLineJson)
FS_JSON_DECLARE(BusLinesJson)
FS_JSON_DECLARE(TimetableFeedJson)
FS_JSON_DECLARE(TimetablePatternJson)
FS_JSON_DECLARE(TimetableLineJson)
FS_JSON_DECLARE(TimetableBusPatternJson)
FS_JSON_DECLARE(TimetableBusLineJson)
FS_JSON_DECLARE(TimetableBusJson)
FS_JSON_DECLARE(TimetableJson)
FS_JSON_DECLARE(EdgeCrossingJson)
FS_JSON_DECLARE(CrossingJson)
FS_JSON_DECLARE(EdgeJson)
FS_JSON_DECLARE(ArmJson)
FS_JSON_DECLARE(NodeJson)
FS_JSON_DECLARE(SignalPhaseJson)
FS_JSON_DECLARE(SignalControllerJson)
FS_JSON_DECLARE(PlaceJson)
FS_JSON_DECLARE(SpawnJson)
FS_JSON_DECLARE(WorldMetaJson)
FS_JSON_DECLARE(WorldJson)
FS_JSON_DECLARE(WorldChunkJson)
FS_JSON_DECLARE(MapIndexJson)

/** rails.json.gz: `{ rails: RailJson[] }` */
struct RailsFileJson {
  std::vector<RailJson> rails;
};
FS_JSON_DECLARE(RailsFileJson)

/** `map/world.json`, a chunk of the road network (`w/<key>.json.gz`), the index of a map in pieces (`map.json`): false when the text is not JSON. */
bool parseWorldJson(std::string_view text, WorldJson& out);
bool parseWorldChunkJson(std::string_view text, WorldChunkJson& out);
bool parseMapIndexJson(std::string_view text, MapIndexJson& out);

}  // namespace fs
