// SPDX-License-Identifier: AGPL-3.0-only
/**
 * `map/world.json` read into the structures the game builds on: the road
 * network, the traffic lights, the crossings, the places, the start points,
 * the tracks for trams and trains and the field of the practice yard.
 *
 * The document comes from the platform (the original fetches it); here the
 * bytes are parsed into exactly the WorldJson the World is constructed from.
 *
 * Origin: the JSON shape of src/world/types.ts (MaurerAnton/driving-simulator
 * @ ff37c5d, v0.7.0) and the fetch in src/game/app.ts (loadWorld).
 */
#include "world/world.h"

#include <cstdlib>

#include "platform/json.h"

namespace fs {

namespace {

double num(const Json& v, double dflt = 0) { return v.isNumber() ? v.number : dflt; }
int integer(const Json& v, int dflt = 0) {
  return v.isNumber() ? static_cast<int>(v.number) : dflt;
}
long long big(const Json& v, long long dflt = 0) {
  return v.isNumber() ? static_cast<long long>(v.number) : dflt;
}
std::string str(const Json& v) { return v.isString() ? v.str : std::string(); }
bool flag(const Json& v) { return v.kind == Json::Kind::Bool && v.boolean; }

/** [x, y, z, x, y, z, …] as it lies in the file */
void numbers(const Json& v, std::vector<double>& out) {
  out.clear();
  if (!v.isArray()) return;
  out.reserve(v.array.size());
  for (const Json& e : v.array)
    if (e.isNumber()) out.push_back(e.number);
}

void integers(const Json& v, std::vector<int>& out) {
  out.clear();
  if (!v.isArray()) return;
  out.reserve(v.array.size());
  for (const Json& e : v.array)
    if (e.isNumber()) out.push_back(static_cast<int>(e.number));
}

// ---- the records themselves, shared by world.json, the chunk files and map.json ----

void parseNodeJson(const Json& n, NodeJson& nj) {
  nj.x = num(n.at("x"));
  nj.y = num(n.at("y"));
  nj.z = num(n.at("z"));
  nj.gx = num(n.at("gx"));
  nj.gz = num(n.at("gz"));
  nj.t = integer(n.at("t"));
  nj.sc = integer(n.at("sc"), -1);
  nj.tc = num(n.at("tc"));
  nj.o = big(n.at("o"));
  nj.arms.clear();
  if (n.at("arms").isArray())
    for (const Json& a : n.at("arms").array) {
      ArmJson aj;
      aj.e = integer(a.at("e"));
      aj.r = integer(a.at("r"));
      aj.g = integer(a.at("g"), -1);
      aj.sl = num(a.at("sl"));
      nj.arms.push_back(aj);
    }
  numbers(n.at("fr"), nj.fr);
}

void parseEdgeJson(const Json& e, EdgeJson& ej) {
  ej.a = integer(e.at("a"));
  ej.b = integer(e.at("b"));
  ej.c = integer(e.at("c"));
  ej.n = str(e.at("n"));
  ej.r = str(e.at("r"));
  numbers(e.at("p"), ej.p);
  numbers(e.at("q"), ej.q);
  ej.lf = integer(e.at("lf"));
  ej.lb = integer(e.at("lb"));
  ej.lw = num(e.at("lw"));
  ej.w = num(e.at("w"));
  ej.mg = num(e.at("mg"));
  ej.mf = num(e.at("mf"));
  ej.mb = num(e.at("mb"));
  ej.z = integer(e.at("z"));
  ej.s = integer(e.at("s"));
  ej.sl = num(e.at("sl"));
  ej.sr = num(e.at("sr"));
  ej.f = integer(e.at("f"));
  ej.ac = integer(e.at("ac"));
  ej.ca = num(e.at("ca"));
  ej.cb = num(e.at("cb"));
  ej.ly = integer(e.at("ly"));
  ej.tf = str(e.at("tf"));
  ej.tb = str(e.at("tb"));
  ej.o = big(e.at("o"));
  ej.x.clear();
  if (e.at("x").isArray())
    for (const Json& x : e.at("x").array) {
      CrossingJson cj;
      cj.s = num(x.at("s"));
      cj.k = integer(x.at("k"));
      cj.w = num(x.at("w"));
      cj.isl = flag(x.at("isl"));
      cj.sc = integer(x.at("sc"), -1);
      cj.g = integer(x.at("g"), -1);
      ej.x.push_back(cj);
    }
  numbers(e.at("bn"), ej.bn);
}

void parseCrossingJson(const Json& c, CrossingJson& cj) {
  cj.e = integer(c.at("e"));
  cj.s = num(c.at("s"));
  cj.k = integer(c.at("k"));
  cj.w = num(c.at("w"));
  cj.isl = flag(c.at("isl"));
  cj.sc = integer(c.at("sc"), -1);
  cj.g = integer(c.at("g"), -1);
}

void parseSpawnJson(const Json& spj, SpawnJson& sp) {
  sp.name = str(spj.at("name"));
  sp.x = num(spj.at("x"));
  sp.z = num(spj.at("z"));
  sp.yaw = num(spj.at("yaw"));
  sp.kind = str(spj.at("kind"));
  sp.rough = flag(spj.at("rough"));
  sp.g = str(spj.at("g"));
}

void parsePlaceJson(const Json& pjv, PlaceJson& pj) {
  pj.name = str(pjv.at("name"));
  pj.kind = str(pjv.at("kind"));
  pj.x = num(pjv.at("x"));
  pj.z = num(pjv.at("z"));
}

void parseYardJson(const Json& yard, YardJson& w) {
  w.x = num(yard.at("x"));
  w.z = num(yard.at("z"));
  w.len = num(yard.at("len"));
  w.wid = num(yard.at("wid"));
  w.rot = integer(yard.at("rot"));
}

void parseSignalControllerJson(const Json& scj, SignalControllerJson& sc) {
  sc.x = num(scj.at("x"));
  sc.z = num(scj.at("z"));
  sc.groups = integer(scj.at("groups"));
  sc.off = num(scj.at("off"));
  sc.ped = flag(scj.at("ped"));
  sc.phases.clear();
  if (scj.at("phases").isArray())
    for (const Json& ph : scj.at("phases").array) {
      SignalPhaseJson p;
      p.d = num(ph.at("d"));
      p.s = str(ph.at("s"));
      sc.phases.push_back(std::move(p));
    }
}

void parseRailJson(const Json& r, RailJson& rj) {
  rj.k = integer(r.at("k"));
  numbers(r.at("p"), rj.p);
  integers(r.at("a"), rj.a);
  integers(r.at("b"), rj.b);
  if (r.has("st")) {
    rj.hasSt = true;
    numbers(r.at("st"), rj.st);
  }
}

void parseMetaJson(const Json& meta, WorldMetaJson& w) {
  if (!meta.isObject()) return;
  w.name = str(meta.at("name"));
  w.generated = str(meta.at("generated"));
  w.minX = num(meta.at("minX"));
  w.maxX = num(meta.at("maxX"));
  w.minZ = num(meta.at("minZ"));
  w.maxZ = num(meta.at("maxZ"));
  w.tileSize = num(meta.at("tileSize"));
  w.originE = num(meta.at("originE"));
  w.originN = num(meta.at("originN"));
  w.attribution.clear();
  if (meta.at("attribution").isArray())
    for (const Json& a : meta.at("attribution").array)
      if (a.isString()) w.attribution.push_back(a.str);
}

}  // namespace

bool parseWorldJson(const std::string& text, WorldJson& out) {
  Json doc;
  if (!Json::parse(text, doc) || !doc.isObject()) return false;
  if (!doc.has("nodes") || !doc.at("nodes").isArray() || !doc.has("edges") ||
      !doc.at("edges").isArray())
    return false;

  WorldJson w;
  w.version = integer(doc.at("version"));

  // ---- meta: where the map is and how it is cut into tiles ----
  parseMetaJson(doc.at("meta"), w.meta);

  // ---- nodes: junctions in counter-clockwise arm order ----
  w.nodes.reserve(doc.at("nodes").array.size());
  for (const Json& n : doc.at("nodes").array) {
    NodeJson nj;
    parseNodeJson(n, nj);
    w.nodes.push_back(std::move(nj));
  }

  // ---- edges: the roads and their centrelines ----
  w.edges.reserve(doc.at("edges").array.size());
  for (const Json& e : doc.at("edges").array) {
    EdgeJson ej;
    parseEdgeJson(e, ej);
    w.edges.push_back(std::move(ej));
  }

  // ---- forbidden turns: [fromEdge, viaNode, toEdge] ----
  if (doc.at("banned").isArray())
    for (const Json& b : doc.at("banned").array) {
      std::vector<int> t;
      integers(b, t);
      if (t.size() == 3) w.banned.push_back(std::move(t));
    }

  // ---- traffic lights ----
  if (doc.at("signals").isArray())
    for (const Json& s : doc.at("signals").array) {
      SignalControllerJson sc;
      parseSignalControllerJson(s, sc);
      w.signals.push_back(std::move(sc));
    }

  // ---- crossings: where people cross, and where the tram does ----
  if (doc.at("crossings").isArray())
    for (const Json& c : doc.at("crossings").array) {
      CrossingJson cj;
      parseCrossingJson(c, cj);
      w.crossings.push_back(cj);
    }

  // ---- places, start points, the tiles on disk ----
  if (doc.at("places").isArray())
    for (const Json& p : doc.at("places").array) {
      PlaceJson pj;
      parsePlaceJson(p, pj);
      w.places.push_back(std::move(pj));
    }
  if (doc.at("spawns").isArray())
    for (const Json& s : doc.at("spawns").array) {
      SpawnJson sp;
      parseSpawnJson(s, sp);
      w.spawns.push_back(std::move(sp));
    }
  if (doc.at("tiles").isArray())
    for (const Json& t : doc.at("tiles").array)
      if (t.isString()) w.tiles.push_back(t.str);

  // ---- the practice yard of the truck lessons ----
  const Json& yard = doc.at("yard");
  if (yard.isObject()) {
    w.hasYard = true;
    parseYardJson(yard, w.yard);
  }

  // ---- tram and railway tracks (maps built before trams ran have none) ----
  if (doc.at("rails").isArray())
    for (const Json& r : doc.at("rails").array) {
      RailJson rj;
      parseRailJson(r, rj);
      w.rails.push_back(std::move(rj));
    }

  out = std::move(w);
  return true;
}

bool parseWorldChunkJson(const std::string& text, WorldChunkJson& out) {
  Json doc;
  if (!Json::parse(text, doc) || !doc.isObject()) return false;
  WorldChunkJson c;
  c.v = integer(doc.at("v"), 1);
  c.nodes.clear();
  if (doc.at("nodes").isArray())
    for (const Json& rec : doc.at("nodes").array) {
      if (!rec.isArray() || rec.array.size() < 2 || !rec.array[1].isObject()) return false;
      NodeJson nj;
      parseNodeJson(rec.array[1], nj);
      c.nodes.emplace_back(integer(rec.array[0]), std::move(nj));
    }
  c.edges.clear();
  if (doc.at("edges").isArray())
    for (const Json& rec : doc.at("edges").array) {
      if (!rec.isArray() || rec.array.size() < 2 || !rec.array[1].isObject()) return false;
      EdgeJson ej;
      parseEdgeJson(rec.array[1], ej);
      c.edges.emplace_back(integer(rec.array[0]), std::move(ej));
    }
  c.signals.clear();
  if (doc.at("signals").isArray())
    for (const Json& rec : doc.at("signals").array) {
      if (!rec.isArray() || rec.array.size() < 2 || !rec.array[1].isObject()) return false;
      SignalControllerJson sc;
      parseSignalControllerJson(rec.array[1], sc);
      c.signals.emplace_back(integer(rec.array[0]), std::move(sc));
    }
  out = std::move(c);
  return true;
}

void parseGridBitsJson(const Json& g, GridBits& out) {
  out.x0 = integer(g.at("x0"));
  out.z0 = integer(g.at("z0"));
  out.w = integer(g.at("w"));
  out.h = integer(g.at("h"));
  out.bits = str(g.at("bits"));
}

bool parseMapIndexJson(const std::string& text, MapIndexJson& out) {
  Json doc;
  if (!Json::parse(text, doc) || !doc.isObject()) return false;
  MapIndexJson m;
  m.version = integer(doc.at("version"));
  parseMetaJson(doc.at("meta"), m.meta);
  if (doc.at("places").isArray())
    for (const Json& p : doc.at("places").array) {
      PlaceJson pj;
      parsePlaceJson(p, pj);
      m.places.push_back(std::move(pj));
    }
  if (doc.at("spawns").isArray())
    for (const Json& s : doc.at("spawns").array) {
      SpawnJson sp;
      parseSpawnJson(s, sp);
      m.spawns.push_back(std::move(sp));
    }
  if (doc.at("yard").isObject()) {
    m.hasYard = true;
    parseYardJson(doc.at("yard"), m.yard);
  }
  parseGridBitsJson(doc.at("tiles"), m.tiles);
  parseGridBitsJson(doc.at("chunks"), m.chunks);
  m.nodes = integer(doc.at("nodes"));
  m.edges = integer(doc.at("edges"));
  out = std::move(m);
  return true;
}

}  // namespace fs
