// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The files of a map as their records, read from JSON and written to it (see worldjson.h).
 * The names of the fields are the names of the members in the files.
 *
 * Origin: the shapes of src/world/types.ts and src/world/xsection.ts @ v1.7.7.
 */
#include "world/worldjson.h"

namespace fs {

FS_JSON_DEFINE(GridBits, x0, z0, w, h, bits)
FS_JSON_DEFINE(RoadSideJson, p, po, pw, pm, pq, c, cw, cr, ca, cs, v, k, g, el, vp, vt, pt, ft, fh, fo, wm)
FS_JSON_DEFINE(YardJson, x, z, len, wid, rot)
FS_JSON_DEFINE(RailJson, k, p, a, b, st, sn, ss, e)
FS_JSON_DEFINE(RailsFileJson, rails)
FS_JSON_DEFINE(StopsJson, v, towns, places, poles, platforms)
FS_JSON_DEFINE(RunwayJson, ref, x1, z1, x2, z2, w, s)
FS_JSON_DEFINE(TaxiwayJson, w, s, p)
FS_JSON_DEFINE(ApronJson, s, p)
FS_JSON_DEFINE(AirfieldJson, name, icao, kind, x, z, runways, taxiways, aprons)
FS_JSON_DEFINE(AirJson, v, airfields)
FS_JSON_DEFINE(LineJson, ref, name, from, to, colour, kind, paths, stops)
FS_JSON_DEFINE(LinesJson, v, lines)
FS_JSON_DEFINE(BusLinesJson, v, map, places, lines)
FS_JSON_DEFINE(TimetableFeedJson, name, version, url, start, end)
FS_JSON_DEFINE(TimetablePatternJson, s, j, d, a, to, pre, post, t)
FS_JSON_DEFINE(TimetableLineJson, line, ref, colour, patterns)
FS_JSON_DEFINE(TimetableBusPatternJson, j, d, w, to, pre, post, t)
FS_JSON_DEFINE(TimetableBusLineJson, line, ref, osm, colour, s, patterns)
FS_JSON_DEFINE(TimetableBusJson, map, places, stops, dests, lines)
FS_JSON_DEFINE(TimetableJson, version, generated, feeds, valid, week, days, attribution, stops, lines, stopPlaces, bus)
FS_JSON_DEFINE(EdgeCrossingJson, s, k, w, isl, sc, g)
FS_JSON_DEFINE(CrossingJson, e, s, k, w, isl, sc, g)
// (the members a chunk has for every road first: the reader looks for a name in this order)
FS_JSON_DEFINE(EdgeJson, a, b, c, p, q, lf, lb, lw, w, mf, mb, f, ca, cb, o, n, r, s, ac, sl, sr, z, ly, x, tf, tb, bn, mg, xl, xr, md, cm, bo, mk, gl, zz, rt, zs)
FS_JSON_DEFINE(ArmJson, e, r, g, sl)
FS_JSON_DEFINE(NodeJson, x, y, z, t, arms, o, gx, gz, fr, tc, sc)
FS_JSON_DEFINE(SignalPhaseJson, d, s)
FS_JSON_DEFINE(SignalControllerJson, x, z, groups, phases, off, ped)
FS_JSON_DEFINE(PlaceJson, name, kind, x, z)
FS_JSON_DEFINE(SpawnJson, name, x, z, yaw, kind, rough, g)
FS_JSON_DEFINE(WorldMetaJson, name, generated, patched, minX, maxX, minZ, maxZ, tileSize, originE, originN, attribution)
FS_JSON_DEFINE(WorldJson, version, meta, nodes, edges, banned, signals, crossings, places, spawns, rails, yard, tiles)
FS_JSON_DEFINE(WorldChunkJson, v, nodes, edges, signals)
FS_JSON_DEFINE(MapIndexJson, version, meta, places, spawns, yard, tiles, chunks, nodes, edges)

// (`operator` is a word of C++: the field is `operator_`)
void fromJson(JsonReader& r, BusLineJson& v) {
  r.object([&](std::string_view k) {
    if (k == "ref") fromJson(r, v.ref);
    else if (k == "name") fromJson(r, v.name);
    else if (k == "from") fromJson(r, v.from);
    else if (k == "to") fromJson(r, v.to);
    else if (k == "operator") fromJson(r, v.operator_);
    else if (k == "colour") fromJson(r, v.colour);
    else if (k == "kind") fromJson(r, v.kind);
    else if (k == "osm") fromJson(r, v.osm);
    else if (k == "w") fromJson(r, v.w);
    else if (k == "end") fromJson(r, v.end);
    else if (k == "st") fromJson(r, v.st);
    else if (k == "back") fromJson(r, v.back);
  });
}

void toJson(JsonWriter& w, const BusLineJson& v) {
  w.beginObject();
  jsonMember(w, "ref", v.ref);
  jsonMember(w, "name", v.name);
  jsonMember(w, "from", v.from);
  jsonMember(w, "to", v.to);
  jsonMember(w, "operator", v.operator_);
  jsonMember(w, "colour", v.colour);
  jsonMember(w, "kind", v.kind);
  jsonMember(w, "osm", v.osm);
  jsonMember(w, "w", v.w);
  jsonMember(w, "end", v.end);
  jsonMember(w, "st", v.st);
  jsonMember(w, "back", v.back);
  w.endObject();
}

bool parseWorldJson(std::string_view text, WorldJson& out) { return jsonParse(text, out); }
bool parseWorldChunkJson(std::string_view text, WorldChunkJson& out) { return jsonParse(text, out); }
bool parseMapIndexJson(std::string_view text, MapIndexJson& out) { return jsonParse(text, out); }

}  // namespace fs
