// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Start or jump to an address: search in the house numbers of the map
 * (OpenStreetMap `addr:*`, see tools/build-addresses.ts) and find the spot on
 * the road in front of the house.
 *
 * Origin: src/game/address.ts (MaurerAnton/driving-simulator @ 1c08919, v0.5.0).
 */
#include "game/address.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>

#include "platform/json.h"

namespace fs {

namespace {

bool digit(const std::string& s) {
  return !s.empty() && std::isdigit(static_cast<unsigned char>(s[0]));
}

bool hasDigit(const std::string& s) {
  for (char c : s)
    if (std::isdigit(static_cast<unsigned char>(c))) return true;
  return false;
}

bool allDigit(const std::string& s) {
  if (s.empty()) return false;
  for (char c : s)
    if (!std::isdigit(static_cast<unsigned char>(c))) return false;
  return true;
}

/** parseInt(s, 10): the leading digits, or NaN (like JS Number.isFinite). */
bool parseInt10(const std::string& s, int& out) {
  size_t i = 0;
  while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) i++;
  size_t start = i;
  while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) i++;
  if (i == start) return false;
  out = std::atoi(s.substr(start, i - start).c_str());
  return true;
}

bool startsWith(const std::string& s, const std::string& p) {
  return s.rfind(p, 0) == 0;
}

bool contains(const std::string& s, const std::string& p) { return s.find(p) != std::string::npos; }

std::vector<std::string> splitWords(const std::string& s) {
  std::vector<std::string> out;
  size_t i = 0;
  while (i < s.size()) {
    while (i < s.size() && s[i] == ' ') i++;
    size_t start = i;
    while (i < s.size() && s[i] != ' ') i++;
    if (i > start) out.push_back(s.substr(start, i - start));
  }
  return out;
}

bool anyWordStartsWith(const std::string& key, const std::string& w) {
  for (const std::string& p : splitWords(key))
    if (startsWith(p, w)) return true;
  return false;
}

/** String.prototype.localeCompare(b, 'de') as the original uses it. */
int localeCompare(const std::string& a, const std::string& b) {
  size_t n = std::min(a.size(), b.size());
  for (size_t i = 0; i < n; i++) {
    unsigned char ca = static_cast<unsigned char>(a[i]);
    unsigned char cb = static_cast<unsigned char>(b[i]);
    if (ca == cb) continue;
    // compare as text: the map's street names are plain ASCII; the accented
    // forms sort after their letters, as UTF-8 byte order does for them
    return ca < cb ? -1 : 1;
  }
  return a.size() == b.size() ? 0 : (a.size() < b.size() ? -1 : 1);
}

const std::string& pick(const std::string& de, const std::string& en, bool wantEn) {
  return wantEn ? en : de;
}

std::string houseKey(const std::string& no) {
  std::string out;
  for (char c : no) {
    char l = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if ((l >= 'a' && l <= 'z') || (l >= '0' && l <= '9')) out.push_back(l);
  }
  return out;
}

/** "12", "12a", "12-14" … in house-number order */
bool compareHouse(const std::string& a, const std::string& b) {
  int na = 0, nb = 0;
  bool fa = parseInt10(a, na), fb = parseInt10(b, nb);
  if (fa && fb && na != nb) return na < nb;
  return localeCompare(a, b) < 0;
}

/** Shared state of findRoadSpot: the projection and sample used by the search. */
PolyProjection projOut;
PolySample smp;

}  // namespace

std::string normaliseAddress(const std::string& text) {
  // lower case
  std::string s;
  for (char c : text) s.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  // "ß" = "ss"  (the original replaces it before normalising)
  {
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
      if (static_cast<unsigned char>(s[i]) == 0xc3 && i + 1 < s.size() &&
          static_cast<unsigned char>(s[i + 1]) == 0x9f) {
        out += "ss";
        i++;
      } else {
        out.push_back(s[i]);
      }
    }
    s = out;
  }
  // decompose to NFKD: "ü" becomes "u" plus a combining diaeresis, "straße"
  // becomes "strasse" (every letter spelled out) — which is what makes
  // "brueder" and "Brüder" come out the same below
  {
    static const struct Decompose {
      const char* c;
      const char* out;
    } TABLE[] = {
        {"\xc3\xa4", "a\xcc\x88"}, {"\xc3\xb6", "o\xcc\x88"}, {"\xc3\xbc", "u\xcc\x88"},
        {"\xc3\x84", "A\xcc\x88"}, {"\xc3\x96", "O\xcc\x88"}, {"\xc3\x9c", "U\xcc\x88"},
        {"\xc3\x9f", "ss"},  // ß (no decomposition; kept for completeness)
        {"\xe2\x80\x99", "'"}, {"\xe2\x80\x9c", "\""}, {"\xe2\x80\x9d", "\""},
        {"\xe2\x80\x9e", "\""}, {"\xe2\x80\x93", "-"}, {"\xe2\x80\x94", "-"},
        {"\xe2\x80\x98", "'"}, {"\xe2\x80\xa6", "..."},
    };
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
      bool replaced = false;
      for (const Decompose& d : TABLE) {
        size_t n = std::strlen(d.c);
        if (s.compare(i, n, d.c) == 0) {
          out += d.out;
          i += n - 1;
          replaced = true;
          break;
        }
      }
      if (!replaced) out.push_back(s[i]);
    }
    s = out;
  }
  // strip combining marks (U+0300–U+036F left by the decomposition)
  {
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
      unsigned char c = static_cast<unsigned char>(s[i]);
      if (c == 0xcc && i + 1 < s.size() && static_cast<unsigned char>(s[i + 1]) >= 0x80) {
        i++;
        continue;
      }
      if (c == 0xcd && i + 1 < s.size() && static_cast<unsigned char>(s[i + 1]) <= 0xaf) {
        i++;
        continue;
      }
      out.push_back(s[i]);
    }
    s = out;
  }
  auto replaceAll = [](std::string& str, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = str.find(from, pos)) != std::string::npos) {
      str.replace(pos, from.size(), to);
      pos += to.size();
    }
  };
  // whatever the keyboard at hand makes of a German street name
  replaceAll(s, "ae", "a");
  replaceAll(s, "oe", "o");
  replaceAll(s, "ue", "u");
  replaceAll(s, "strasse", "str");
  replaceAll(s, "str.", "str");
  // keep letters and digits, separate runs with one space, trim
  std::string out;
  for (char c : s) {
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
      out.push_back(c);
    else if (!out.empty() && out.back() != ' ')
      out.push_back(' ');
  }
  while (!out.empty() && out.back() == ' ') out.pop_back();
  return out;
}

bool parseAddressJson(const std::string& text, AddressJson& out) {
  Json doc;
  if (!Json::parse(text, doc) || !doc.isObject()) return false;
  out = AddressJson();
  const Json& v = doc.at("version");
  out.version = v.isNumber() ? static_cast<int>(v.number) : 0;
  const Json& streets = doc.at("streets");
  if (!streets.isArray()) return false;
  for (const auto& s : streets.array) {
    if (!s.isObject()) continue;
    AddressJson::StreetJson st;
    st.n = s.at("n").asStr();
    st.c = s.at("c").asStr();
    st.x = s.at("x").asNum();
    st.z = s.at("z").asNum();
    const Json& h = s.at("h");
    if (h.isArray())
      for (const auto& e : h.array)
        st.h.push_back(e.isString() ? e.str : e.asStr());
    out.streets.push_back(std::move(st));
  }
  return true;
}

AddressIndex::AddressIndex(const AddressJson& json) {
  for (const AddressJson::StreetJson& s : json.streets) {
    std::vector<House> houses;
    for (size_t i = 0; i + 2 < s.h.size(); i += 3) {
      House h;
      h.no = s.h[i];
      h.key = houseKey(h.no);
      h.x = std::atof(s.h[i + 1].c_str());
      h.z = std::atof(s.h[i + 2].c_str());
      houses.push_back(h);
    }
    std::stable_sort(houses.begin(), houses.end(),
                     [](const House& a, const House& b) { return compareHouse(a.no, b.no); });
    Street st;
    st.name = s.n;
    st.town = s.c;
    st.key = normaliseAddress(s.n);
    st.flat = st.key;
    {
      std::string& f = st.flat;
      f.erase(std::remove(f.begin(), f.end(), ' '), f.end());
    }
    st.townKey = normaliseAddress(s.c);
    st.x = s.x;
    st.z = s.z;
    st.houses = std::move(houses);
    streets_.push_back(std::move(st));
  }
}

size_t AddressIndex::size() const {
  size_t n = 0;
  for (const Street& s : streets_) n += s.houses.size();
  return n;
}

std::vector<AddressHit> AddressIndex::search(const std::string& query, int limit,
                                             const double* nearX, const double* nearZ) const {
  const std::string full = normaliseAddress(query);
  auto place = [](const Street& s) { return s.town.empty() ? std::string("") : ", " + s.town; };
  // a road known by its number ("A 49", "L 3219"): that is no house number
  if (hasDigit(full)) {
    std::string flat = full;
    flat.erase(std::remove(flat.begin(), flat.end(), ' '), flat.end());
    std::vector<const Street*> roads;
    for (const Street& s : streets_)
      if (hasDigit(s.key) && (startsWith(s.key, full) || startsWith(s.flat, flat)))
        roads.push_back(&s);
    if (!roads.empty()) {
      std::stable_sort(roads.begin(), roads.end(), [](const Street* a, const Street* b) {
        if (a->flat.size() != b->flat.size()) return a->flat.size() < b->flat.size();
        int c = localeCompare(a->name, b->name);
        if (c != 0) return c < 0;
        return localeCompare(a->town, b->town) < 0;
      });
      std::vector<AddressHit> out;
      for (const Street* s : roads) {
        if (static_cast<int>(out.size()) >= limit) break;
        AddressHit hit;
        hit.label = s->name + place(*s);
        hit.street = s->name;
        hit.x = s->x;
        hit.z = s->z;
        hit.exact = false;
        out.push_back(hit);
      }
      return out;
    }
  }
  const std::vector<std::string> tokens = splitWords(full);
  // the house number: the first token that starts with a digit ("5", "5a", or "5" + "a")
  std::string house;
  std::vector<std::string> words;
  for (size_t i = 0; i < tokens.size(); i++) {
    const std::string& t = tokens[i];
    // /^\d+[a-z]?$/ — the house number: "5", "5a", a bare "10"; a letter that
    // stands apart ("10 a") only joins a number that is already the house
    bool isHouse = false;
    if (!t.empty() && std::isdigit(static_cast<unsigned char>(t[0]))) {
      if (house.empty()) {
        if (t.size() == 1) {
          isHouse = true;
        } else {
          // all digits, or digits plus exactly one lowercase letter
          isHouse = true;
          bool letterSeen = false;
          for (size_t k = 1; k < t.size(); k++) {
            char ch = t[k];
            if (ch >= '0' && ch <= '9') continue;
            if (ch >= 'a' && ch <= 'z' && !letterSeen && k == t.size() - 1) {
              letterSeen = true;
              continue;
            }
            isHouse = false;
            break;
          }
        }
      }
    }
    if (isHouse) {
      if (house.empty()) {
        house = t;
        // "5" + "a" written apart is one house number too
        if (allDigit(t) && i + 1 < tokens.size() && tokens[i + 1].size() == 1 &&
            std::islower(static_cast<unsigned char>(tokens[i + 1][0])))
          house += tokens[++i];
        continue;
      }
    }
    words.push_back(t);
  }
  if (words.empty()) return {};
  struct Scored {
    const Street* s;
    double score;
  };
  std::vector<Scored> scored;
  for (const Street& s : streets_) {
    double score = 0;
    bool ok = true;
    for (size_t i = 0; i < words.size(); i++) {
      const std::string& w = words[i];
      if (startsWith(s.key, w) || startsWith(s.flat, w))
        score += i == 0 ? 6 : 4;
      else if (anyWordStartsWith(s.key, w))
        score += 4;
      else if (w.size() >= 3 && contains(s.flat, w))
        score += 2;
      else if (anyWordStartsWith(s.townKey, w))
        score += 1;
      else {
        ok = false;
        break;
      }
    }
    if (!ok) continue;
    // the whole query is the whole name: that is the one meant
    {
      std::string joined;
      for (size_t i = 0; i < words.size(); i++) {
        if (i) joined += " ";
        joined += words[i];
      }
      std::string flatAll;
      for (const std::string& w : words) flatAll += w;
      if (s.key == joined || s.flat == flatAll) score += 5;
    }
    if (!house.empty()) {
      for (const House& h : s.houses)
        if (h.key == house) {
          score += 3;
          break;
        }
    }
    scored.push_back({&s, score - s.flat.size() * 0.01});
  }
  auto far = [&](const Street& s) {
    return (nearX && nearZ) ? std::hypot(s.x - *nearX, s.z - *nearZ) : 0.0;
  };
  std::stable_sort(scored.begin(), scored.end(), [&](const Scored& a, const Scored& b) {
    if (a.score != b.score) return a.score > b.score;
    double fa = far(*a.s), fb = far(*b.s);
    if (fa != fb) return fa < fb;
    int c = localeCompare(a.s->name, b.s->name);
    if (c != 0) return c < 0;
    return localeCompare(a.s->town, b.s->town) < 0;
  });
  std::vector<AddressHit> out;
  for (const Scored& sc : scored) {
    if (static_cast<int>(out.size()) >= limit) break;
    const Street& s = *sc.s;
    if (house.empty()) {
      out.push_back(AddressHit{s.name + place(s), s.name, s.x, s.z, false});
      continue;
    }
    // "5" also offers 5a, 5b – and, further down, 50 … 59
    std::vector<const House*> exact, same, longer;
    for (const House& h : s.houses) {
      if (h.key == house) exact.push_back(&h);
    }
    const bool digits = allDigit(house);
    int houseNum = 0;
    parseInt10(house, houseNum);
    if (digits) {
      for (const House& h : s.houses) {
        if (h.key == house) continue;
        int hn = 0;
        bool f = parseInt10(h.key, hn);
        if (f && hn == houseNum) same.push_back(&h);
        else if (startsWith(h.key, house) && (!f || hn != houseNum)) longer.push_back(&h);
      }
    }
    std::vector<const House*> list;
    for (const House* h : exact) list.push_back(h);
    for (const House* h : same) list.push_back(h);
    for (const House* h : longer) list.push_back(h);
    size_t take = static_cast<size_t>(std::max(1, std::min(4, limit - static_cast<int>(out.size()))));
    if (list.size() > take) list.resize(take);
    for (const House* h : list)
      out.push_back(AddressHit{s.name + " " + h->no + place(s), s.name, h->x, h->z, h->key == house});
    if (list.empty()) out.push_back(AddressHit{s.name + place(s), s.name, s.x, s.z, false});
  }
  if (out.size() > static_cast<size_t>(limit)) out.resize(static_cast<size_t>(limit));
  return out;
}

std::set<int> mainNetwork(World& world, LaneGraph& graph) {
  // start from the lanes of the road nearest to the first starting point
  const std::vector<SpawnJson>& spawns = world.json().spawns;
  const SpawnJson sp = spawns.empty() ? SpawnJson() : spawns[0];
  Edge* seed = nullptr;
  double bd = std::numeric_limits<double>::infinity();
  for (Edge& e : world.edges()) {
    if (!LaneGraph::drivable(e, true)) continue;
    const size_t mid = (e.pl.pts.size() / 3) / 2;
    const double d = std::hypot(e.pl.pts[mid * 3] - sp.x, e.pl.pts[mid * 3 + 2] - sp.z);
    if (d < bd) {
      bd = d;
      seed = &e;
    }
  }
  auto walk = [&](bool forward) {
    std::set<int> seen;
    std::vector<Lane*> stack;
    if (seed) {
      const LaneGraph::EdgeLanes& el = graph.edgeLanes[static_cast<size_t>(seed->id)];
      for (Lane* l : el.fwd) stack.push_back(l);
      for (Lane* l : el.back) stack.push_back(l);
    }
    for (Lane* l : stack) seen.insert(l->id);
    while (!stack.empty()) {
      Lane* l = stack.back();
      stack.pop_back();
      const std::vector<Connector*>& conns = forward ? l->out : l->in;
      for (Connector* c : conns) {
        Lane* n = forward ? c->to : c->from;
        // only by roads one may use: a village joined to the rest by a farm
        // track is not connected
        if (!n || seen.count(n->id) || !LaneGraph::drivable(*n->edge, false)) continue;
        seen.insert(n->id);
        stack.push_back(n);
      }
    }
    return seen;
  };
  const std::set<int> there = walk(true);
  const std::set<int> back = walk(false);
  std::set<int> out;
  for (int id : there)
    if (back.count(id)) out.insert(id);
  return out;
}

SpawnJson findRoadSpot(World& world, LaneGraph& graph, double x, double z,
                       const std::string& street, double maxDist, const std::set<int>* main,
                       const std::function<bool(const Edge&)>& accept, double room) {
  const std::string want = normaliseAddress(street);
  struct Best {
    Edge* edge = nullptr;
    double s = 0, off = 0, score = 0;
    bool set = false;
  } best;
  auto usable = [&](const Lane& l) { return !main || main->count(l.id) > 0; };
  for (bool roomy : {true, false}) {
    world.edgesInBox(x - maxDist, z - maxDist, x + maxDist, z + maxDist, [&](Edge& e) {
      if (!LaneGraph::drivable(e, false)) return;
      if (accept && !accept(e)) return;
      const LaneGraph::EdgeLanes& lanes = graph.edgeLanes[static_cast<size_t>(e.id)];
      auto someUsable = [&](const std::vector<Lane*>& ls) {
        for (const Lane* l : ls)
          if (l && usable(*l)) return true;
        return false;
      };
      if (!someUsable(lanes.fwd) && !someUsable(lanes.back)) return;
      // keep out of the junction areas at both ends
      const double margin = roomy ? 6 : 1;
      const double lo = e.cutA + margin;
      const double hi = e.len - e.cutB - margin;
      if (hi - lo < (roomy ? 8 : 0.5)) return;
      PolyProjection p = projectOnPolyline(e.pl, x, z, projOut);
      const double s = std::max(lo, std::min(hi, p.s));
      const double d = std::sqrt(p.d2) + std::fabs(s - p.s);
      if (d > maxDist) return;
      double score = d;
      const bool named = !want.empty() &&
                         (normaliseAddress(e.name) == want || normaliseAddress(e.ref) == want);
      if (!want.empty() && !named) score += 70;
      if (e.cls <= TRUNK_LINK && !named) score += 400;  // nobody lives on the motorway
      if ((e.flags & (EF_BRIDGE | EF_TUNNEL)) != 0) score += 60;
      // a proper street rather than the yard or car park next to the house
      if (!LaneGraph::drivable(e, true)) score += 150;
      if (!best.set || score < best.score) best = {&e, s, p.off, score, true};
    });
    if (best.set) break;
  }
  if (!best.set) return SpawnJson();
  const LaneGraph::EdgeLanes& lanes = graph.edgeLanes[static_cast<size_t>(best.edge->id)];
  // the house on the right: it lies left of a→b, so drive b→a (if that way is open)
  std::vector<Lane*> sideA = best.off > 0 ? lanes.back : lanes.fwd;
  std::vector<Lane*> sideB = best.off > 0 ? lanes.fwd : lanes.back;
  std::vector<Lane*> first, second;
  for (Lane* l : sideA)
    if (l && usable(*l)) first.push_back(l);
  for (Lane* l : sideB)
    if (l && usable(*l)) second.push_back(l);
  const std::vector<Lane*>& dir = first.empty() ? second : first;
  if (dir.empty()) return SpawnJson();
  Lane* lane = dir[0];
  for (Lane* l : dir)
    if (l->idx == 0) {
      lane = l;
      break;
    }
  const double d = lane->dir > 0 ? best.s - lane->s0 : lane->s0 - best.s;
  // (`room`: metres of lane wanted behind the spot – a trailer must not hang
  // back into the junction)
  const double behind = std::min(room, lane->len / 2);
  smp = lanePoint(*lane, std::max(behind, std::min(lane->len - room, d)), smp);
  SpawnJson out;
  // (named after its street, for whoever shows the spot on a map)
  out.name = best.edge->name.empty() ? best.edge->ref : best.edge->name;
  out.x = smp.x;
  out.z = smp.z;
  out.yaw = std::atan2(-smp.tx, -smp.tz);
  out.kind = "street";
  return out;
}

}  // namespace fs
