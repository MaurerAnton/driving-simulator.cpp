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
  const Json& meta = doc.at("meta");
  if (meta.isObject()) {
    w.meta.name = str(meta.at("name"));
    w.meta.generated = str(meta.at("generated"));
    w.meta.minX = num(meta.at("minX"));
    w.meta.maxX = num(meta.at("maxX"));
    w.meta.minZ = num(meta.at("minZ"));
    w.meta.maxZ = num(meta.at("maxZ"));
    w.meta.tileSize = num(meta.at("tileSize"));
    w.meta.originE = num(meta.at("originE"));
    w.meta.originN = num(meta.at("originN"));
    if (meta.at("attribution").isArray())
      for (const Json& a : meta.at("attribution").array)
        if (a.isString()) w.meta.attribution.push_back(a.str);
  }

  // ---- nodes: junctions in counter-clockwise arm order ----
  w.nodes.reserve(doc.at("nodes").array.size());
  for (const Json& n : doc.at("nodes").array) {
    NodeJson nj;
    nj.x = num(n.at("x"));
    nj.y = num(n.at("y"));
    nj.z = num(n.at("z"));
    nj.gx = num(n.at("gx"));
    nj.gz = num(n.at("gz"));
    nj.t = integer(n.at("t"));
    nj.sc = integer(n.at("sc"), -1);
    nj.tc = num(n.at("tc"));
    nj.o = big(n.at("o"));
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
    w.nodes.push_back(std::move(nj));
  }

  // ---- edges: the roads and their centrelines ----
  w.edges.reserve(doc.at("edges").array.size());
  for (const Json& e : doc.at("edges").array) {
    EdgeJson ej;
    ej.a = integer(e.at("a"));
    ej.b = integer(e.at("b"));
    ej.c = integer(e.at("c"));
    ej.n = str(e.at("n"));
    ej.r = str(e.at("r"));
    numbers(e.at("p"), ej.p);
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
      sc.x = num(s.at("x"));
      sc.z = num(s.at("z"));
      sc.groups = integer(s.at("groups"));
      sc.off = num(s.at("off"));
      sc.ped = flag(s.at("ped"));
      if (s.at("phases").isArray())
        for (const Json& p : s.at("phases").array) {
          SignalPhaseJson ph;
          ph.d = num(p.at("d"));
          ph.s = str(p.at("s"));
          sc.phases.push_back(std::move(ph));
        }
      w.signals.push_back(std::move(sc));
    }

  // ---- crossings: where people cross, and where the tram does ----
  if (doc.at("crossings").isArray())
    for (const Json& c : doc.at("crossings").array) {
      CrossingJson cj;
      cj.e = integer(c.at("e"));
      cj.s = num(c.at("s"));
      cj.k = integer(c.at("k"));
      cj.w = num(c.at("w"));
      cj.isl = flag(c.at("isl"));
      cj.sc = integer(c.at("sc"), -1);
      cj.g = integer(c.at("g"), -1);
      w.crossings.push_back(cj);
    }

  // ---- places, start points, the tiles on disk ----
  if (doc.at("places").isArray())
    for (const Json& p : doc.at("places").array) {
      PlaceJson pj;
      pj.name = str(p.at("name"));
      pj.kind = str(p.at("kind"));
      pj.x = num(p.at("x"));
      pj.z = num(p.at("z"));
      w.places.push_back(std::move(pj));
    }
  if (doc.at("spawns").isArray())
    for (const Json& s : doc.at("spawns").array) {
      SpawnJson sp;
      sp.name = str(s.at("name"));
      sp.x = num(s.at("x"));
      sp.z = num(s.at("z"));
      sp.yaw = num(s.at("yaw"));
      sp.kind = str(s.at("kind"));
      w.spawns.push_back(std::move(sp));
    }
  if (doc.at("tiles").isArray())
    for (const Json& t : doc.at("tiles").array)
      if (t.isString()) w.tiles.push_back(t.str);

  // ---- the practice yard of the truck lessons ----
  const Json& yard = doc.at("yard");
  if (yard.isObject()) {
    w.hasYard = true;
    w.yard.x = num(yard.at("x"));
    w.yard.z = num(yard.at("z"));
    w.yard.len = num(yard.at("len"));
    w.yard.wid = num(yard.at("wid"));
    w.yard.rot = integer(yard.at("rot"));
  }

  // ---- tram and railway tracks (maps built before trams ran have none) ----
  if (doc.at("rails").isArray())
    for (const Json& r : doc.at("rails").array) {
      RailJson rj;
      rj.k = integer(r.at("k"));
      numbers(r.at("p"), rj.p);
      integers(r.at("a"), rj.a);
      integers(r.at("b"), rj.b);
      if (r.has("st")) {
        rj.hasSt = true;
        numbers(r.at("st"), rj.st);
      }
      w.rails.push_back(std::move(rj));
    }

  out = std::move(w);
  return true;
}

}  // namespace fs
