// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/address.ts @ v1.7.7
#include "game/address.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

#include "core/geom.h"
#include "core/js.h"

namespace fs {

namespace {

#include "game/address_chars.inc"

// --- the characters of a text ------------------------------------------------------------

/** The next character (code point) of a UTF-8 text; what is no UTF-8 is U+FFFD, as in a browser. */
char32_t nextChar(std::string_view s, size_t& i) {
  const unsigned char c = static_cast<unsigned char>(s[i++]);
  if (c < 0x80) return c;
  int more = c >= 0xf0 && c < 0xf8 ? 3 : c >= 0xe0 && c < 0xf0 ? 2 : c >= 0xc0 && c < 0xe0 ? 1 : -1;
  if (more < 0) return 0xfffd;
  char32_t cp = c & (0x3f >> more);
  for (; more > 0; more--) {
    if (i >= s.size() || (static_cast<unsigned char>(s[i]) & 0xc0) != 0x80) return 0xfffd;
    cp = (cp << 6) | (static_cast<unsigned char>(s[i++]) & 0x3f);
  }
  return cp;
}

/** `text.toLowerCase().replace(/ß/g, 'ss').normalize('NFKD').replace(/[̀-ͯ]/g, '')` – what is left that is no ASCII as a space */
std::string fold(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (size_t i = 0; i < text.size();) {
    const char32_t cp = nextChar(text, i);
    if (cp < 0x80) {
      out += cp >= 'A' && cp <= 'Z' ? static_cast<char>(cp - 'A' + 'a') : static_cast<char>(cp);
      continue;
    }
    const char32_t* end = FOLD_CP + sizeof FOLD_CP / sizeof FOLD_CP[0];
    const char32_t* at = std::lower_bound(FOLD_CP, end, cp);
    if (at == end || *at != cp) out += ' ';
    else out.append(FOLD_TEXT + FOLD_AT[at - FOLD_CP], FOLD_TEXT + FOLD_AT[at - FOLD_CP + 1]);
  }
  return out;
}

// --- sorting as German does ----------------------------------------------------------------

/** The weights of a text for one level of the comparison (0: primary, 1: secondary, 2: tertiary): those that are not nothing, in order. */
void weights(std::string_view text, std::vector<uint32_t> out[3]) {
  for (size_t i = 0; i < text.size();) {
    const char32_t cp = nextChar(text, i);
    bool known = false;
    for (const auto& range : COLL_RANGE) {
      if (cp < range.first || cp > range.last) continue;
      const size_t k = range.at + (cp - range.first);
      for (size_t e = COLL_AT[k]; e < COLL_AT[k + 1]; e++) {
        if (COLL_CE[e].p) out[0].push_back(COLL_CE[e].p);
        if (COLL_CE[e].s) out[1].push_back(COLL_CE[e].s);
        if (COLL_CE[e].t) out[2].push_back(COLL_CE[e].t);
      }
      known = true;
      break;
    }
    if (known) continue;
    // (a character of another script: behind all those, in the order of the characters themselves)
    out[0].push_back(0xfe000000u + static_cast<uint32_t>(cp));
    out[1].push_back(0x0500);
    out[2].push_back(0x0500);
  }
}

bool allDigits(const std::string& s) { return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; }); }
bool anyDigit(const std::string& s) { return std::any_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; }); }

/** `/^\d+[a-z]?$/` */
bool houseToken(const std::string& t) {
  size_t i = 0;
  while (i < t.size() && t[i] >= '0' && t[i] <= '9') i++;
  if (i == 0) return false;
  if (i < t.size() && t[i] >= 'a' && t[i] <= 'z') i++;
  return i == t.size();
}

/** `key.split(' ').some((p) => p.startsWith(w))` */
bool someWordStartsWith(const std::string& key, const std::string& w) {
  for (const std::string& p : split(key, " "))
    if (startsWith(p, w)) return true;
  return false;
}

/** `no.toLowerCase().replace(/[^a-z0-9]/g, '')` */
std::string houseKey(const std::string& no) {
  std::string out;
  for (size_t i = 0; i < no.size();) {
    char32_t cp = nextChar(no, i);
    if (cp >= 'A' && cp <= 'Z') cp = cp - 'A' + 'a';
    // (the two characters beyond ASCII whose small letters are ASCII: İ and the Kelvin sign)
    else if (cp == 0x130) cp = 'i';
    else if (cp == 0x212a) cp = 'k';
    if ((cp >= 'a' && cp <= 'z') || (cp >= '0' && cp <= '9')) out += static_cast<char>(cp);
  }
  return out;
}

/** "12", "12a", "12-14" … in house-number order */
int compareHouse(const std::string& a, const std::string& b) {
  const double na = jsParseInt(a);
  const double nb = jsParseInt(b);
  if (std::isfinite(na) && std::isfinite(nb) && na != nb) return na < nb ? -1 : 1;
  return compareGerman(a, b);
}

/** the text of a value of the file's `h`: `String(v)` */
std::string textOf(const std::variant<std::string, double>& v) {
  if (const std::string* s = std::get_if<std::string>(&v)) return *s;
  return jsStr(std::get<double>(v));
}
/** … and its number: `Number(v)` */
double numberOf(const std::variant<std::string, double>& v) {
  if (const double* n = std::get_if<double>(&v)) return *n;
  return jsNumber(std::get<std::string>(v));
}

}  // namespace

std::string normaliseAddress(const std::string& text) {
  std::string s = fold(text);
  s = replaceAll(std::move(s), "ae", "a");
  s = replaceAll(std::move(s), "oe", "o");
  s = replaceAll(std::move(s), "ue", "u");
  s = replaceAll(std::move(s), "strasse", "str");
  s = replaceAll(std::move(s), "str.", "str");
  // `.replace(/[^a-z0-9]+/g, ' ').trim()`
  std::string out;
  for (char c : s) {
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out += c;
    else if (!out.empty() && out.back() != ' ') out += ' ';
  }
  if (!out.empty() && out.back() == ' ') out.pop_back();
  return out;
}

int compareGerman(std::string_view a, std::string_view b) {
  if (a == b) return 0;
  std::vector<uint32_t> wa[3];
  std::vector<uint32_t> wb[3];
  weights(a, wa);
  weights(b, wb);
  // first by the letters, then by their accents, then by small and capital
  for (int level = 0; level < 3; level++) {
    if (wa[level] == wb[level]) continue;
    return std::lexicographical_compare(wa[level].begin(), wa[level].end(), wb[level].begin(), wb[level].end()) ? -1 : 1;
  }
  return 0;
}

void fromJson(JsonReader& r, AddressJson::Street& s) {
  r.object([&](std::string_view key) {
    if (key == "n") r.string(s.n);
    else if (key == "c") r.string(s.c);
    else if (key == "x") r.number(s.x);
    else if (key == "z") r.number(s.z);
    else if (key == "h") {
      s.h.clear();
      r.array([&] {
        if (r.peek() == JsonReader::Type::String) s.h.emplace_back(r.string());
        else if (r.peek() == JsonReader::Type::Number) s.h.emplace_back(r.number());
        else {
          // (nothing a house number or a place is made of: no number)
          r.skip();
          s.h.emplace_back(JS_NAN);
        }
      });
    }
  });
}

void fromJson(JsonReader& r, AddressJson& j) {
  r.object([&](std::string_view key) {
    if (key == "version") {
      double v = 0;
      r.number(v);
      j.version = static_cast<int>(v);
    } else if (key == "streets") {
      j.streets.clear();
      r.array([&] {
        j.streets.emplace_back();
        fromJson(r, j.streets.back());
      });
    }
  });
}

AddressIndex::AddressIndex(const AddressJson& json) {
  for (const AddressJson::Street& s : json.streets) add(s);
}

std::unique_ptr<AddressIndex> AddressIndex::parse(std::string_view text) {
  std::unique_ptr<AddressIndex> index(new AddressIndex());
  JsonReader r(text);
  bool streets = false;
  const bool object = r.object([&](std::string_view key) {
    if (key != "streets") return;
    streets = r.array([&] {
      AddressJson::Street s;
      fromJson(r, s);
      index->add(s);
    });
  });
  if (!object || !streets || !r.ok() || !r.atEnd()) return nullptr;
  return index;
}

void AddressIndex::add(const AddressJson::Street& s) {
  std::vector<House> houses;
  for (size_t i = 0; i + 2 < s.h.size(); i += 3) {
    const std::string no = textOf(s.h[i]);
    houses.push_back({.no = no, .key = houseKey(no), .x = numberOf(s.h[i + 1]), .z = numberOf(s.h[i + 2])});
  }
  std::stable_sort(houses.begin(), houses.end(), [](const House& a, const House& b) { return compareHouse(a.no, b.no) < 0; });
  const std::string key = normaliseAddress(s.n);
  streets.push_back({.name = s.n, .town = s.c, .key = key, .flat = replaceAll(key, " ", ""), .townKey = normaliseAddress(s.c), .x = s.x, .z = s.z, .houses = std::move(houses)});
}

size_t AddressIndex::size() const {
  size_t n = 0;
  for (const Street& s : streets) n += s.houses.size();
  return n;
}

std::vector<AddressHit> AddressIndex::search(const std::string& query, size_t limit, const std::optional<Near>& near) const {
  const std::string full = normaliseAddress(query);
  const auto place = [](const Street& s) { return !s.town.empty() ? ", " + s.town : std::string(); };
  // a road known by its number ("A 49", "L 3219"): that is no house number
  if (anyDigit(full)) {
    const std::string flat = replaceAll(full, " ", "");
    std::vector<const Street*> roads;
    for (const Street& s : streets)
      if (anyDigit(s.key) && (startsWith(s.key, full) || startsWith(s.flat, flat))) roads.push_back(&s);
    if (!roads.empty()) {
      std::stable_sort(roads.begin(), roads.end(), [](const Street* a, const Street* b) {
        if (a->flat.size() != b->flat.size()) return a->flat.size() < b->flat.size();
        const int byName = compareGerman(a->name, b->name);
        if (byName != 0) return byName < 0;
        return compareGerman(a->town, b->town) < 0;
      });
      std::vector<AddressHit> out;
      for (size_t i = 0; i < roads.size() && i < limit; i++) {
        const Street& s = *roads[i];
        out.push_back({.label = s.name + place(s), .street = s.name, .x = s.x, .z = s.z, .exact = false});
      }
      return out;
    }
  }
  std::vector<std::string> tokens;
  for (const std::string& t : split(full, " "))
    if (!t.empty()) tokens.push_back(t);
  // the house number: the first token that starts with a digit ("5", "5a", or "5" + "a")
  std::string house;
  std::vector<std::string> words;
  for (size_t i = 0; i < tokens.size(); i++) {
    const std::string& t = tokens[i];
    if (house.empty() && houseToken(t)) {
      house = t;
      if (allDigits(t) && i + 1 < tokens.size() && tokens[i + 1].size() == 1 && tokens[i + 1][0] >= 'a' && tokens[i + 1][0] <= 'z') house += tokens[++i];
    } else words.push_back(t);
  }
  if (words.empty()) return {};
  struct Scored {
    const Street* s;
    double score;
  };
  std::vector<Scored> scored;
  const std::string spaced = join(words, " ");
  const std::string joined = join(words, "");
  for (const Street& s : streets) {
    double score = 0;
    bool ok = true;
    for (size_t i = 0; i < words.size(); i++) {
      const std::string& w = words[i];
      if (startsWith(s.key, w) || startsWith(s.flat, w)) score += i == 0 ? 6 : 4;
      else if (someWordStartsWith(s.key, w)) score += 4;
      else if (w.size() >= 3 && s.flat.find(w) != std::string::npos) score += 2;
      else if (someWordStartsWith(s.townKey, w)) score += 1;
      else {
        ok = false;
        break;
      }
    }
    if (!ok) continue;
    // the whole query is the whole name: that is the one meant
    if (s.key == spaced || s.flat == joined) score += 5;
    if (!house.empty() && std::any_of(s.houses.begin(), s.houses.end(), [&house](const House& h) { return h.key == house; })) score += 3;
    scored.push_back({&s, score - static_cast<double>(s.flat.size()) * 0.01});
  }
  const auto far = [&near](const Street& s) { return near ? jsHypot(s.x - near->x, s.z - near->z) : 0.0; };
  std::stable_sort(scored.begin(), scored.end(), [&far](const Scored& a, const Scored& b) {
    if (b.score - a.score != 0) return b.score - a.score < 0;
    const double byFar = far(*a.s) - far(*b.s);
    if (byFar != 0) return byFar < 0;
    const int byName = compareGerman(a.s->name, b.s->name);
    if (byName != 0) return byName < 0;
    return compareGerman(a.s->town, b.s->town) < 0;
  });
  std::vector<AddressHit> out;
  for (const Scored& one : scored) {
    const Street& s = *one.s;
    if (out.size() >= limit) break;
    if (house.empty()) {
      out.push_back({.label = s.name + place(s), .street = s.name, .x = s.x, .z = s.z, .exact = false});
      continue;
    }
    // "5" also offers 5a, 5b – and, further down, 50 … 59
    const bool digits = allDigits(house);
    const double number = jsNumber(house);
    std::vector<const House*> list;
    for (const House& h : s.houses)
      if (h.key == house) list.push_back(&h);
    if (digits) {
      for (const House& h : s.houses)
        if (h.key != house && jsParseInt(h.key) == number) list.push_back(&h);
      for (const House& h : s.houses)
        if (startsWith(h.key, house) && jsParseInt(h.key) != number) list.push_back(&h);
    }
    const size_t most = std::max<size_t>(1, std::min<size_t>(4, limit - out.size()));
    if (list.size() > most) list.resize(most);
    for (const House* h : list) out.push_back({.label = s.name + " " + h->no + place(s), .street = s.name, .x = h->x, .z = h->z, .exact = h->key == house});
    if (list.empty()) out.push_back({.label = s.name + place(s), .street = s.name, .x = s.x, .z = s.z, .exact = false});
  }
  if (out.size() > limit) out.resize(limit);
  return out;
}

// ------------------------------------------------------------------ road spot

std::unordered_set<int> mainNetwork(World& world, LaneGraph& graph) {
  // start from the lanes of the road nearest to the first starting point
  double spx = 0;
  double spz = 0;
  if (!world.json.spawns.empty()) {
    spx = world.json.spawns[0].x;
    spz = world.json.spawns[0].z;
  }
  const Edge* seed = nullptr;
  double bd = std::numeric_limits<double>::infinity();
  for (const Edge* e : world.allEdges) {
    if (!LaneGraph::drivable(*e, true)) continue;
    const size_t mid = (e->pl.pts.size() / 3) >> 1;
    const double d = jsHypot(e->pl.pts[mid * 3] - spx, e->pl.pts[mid * 3 + 2] - spz);
    if (d < bd) {
      bd = d;
      seed = e;
    }
  }
  const auto walk = [&](bool forward) {
    std::unordered_set<int> seen;
    std::vector<const Lane*> stack;
    if (seed) {
      if (const LaneGraph::EdgeLanes* el = graph.edgeLanes[seed->id]) {
        stack.insert(stack.end(), el->fwd.begin(), el->fwd.end());
        stack.insert(stack.end(), el->back.begin(), el->back.end());
      }
    }
    for (const Lane* l : stack) seen.insert(l->id);
    while (!stack.empty()) {
      const Lane* l = stack.back();
      stack.pop_back();
      for (const Connector* c : forward ? l->out : l->in) {
        const Lane* n = forward ? c->to : c->from;
        // only by roads one may use: a village joined to the rest by a farm track is not connected
        if (!seen.count(n->id) && LaneGraph::drivable(*n->edge, false)) {
          seen.insert(n->id);
          stack.push_back(n);
        }
      }
    }
    return seen;
  };
  const std::unordered_set<int> there = walk(true);
  const std::unordered_set<int> back = walk(false);
  std::unordered_set<int> out;
  for (int id : there)
    if (back.count(id)) out.insert(id);
  return out;
}

std::optional<SpawnJson> findRoadSpot(World& world, LaneGraph& graph, double x, double z, const std::string& street, double maxDist, const std::function<bool(const Lane& lane)>& main,
                                      const std::function<bool(const Edge& e)>& accept, double room) {
  const std::string want = normaliseAddress(street);
  struct Best {
    Edge* edge;
    double s;
    double off;
    double score;
  };
  std::optional<Best> best;
  PolyProjection proj;
  const auto usable = [&main](const Lane* l) { return !main || main(*l); };
  for (bool roomy : {true, false}) {
    world.edgesInBox(x - maxDist, z - maxDist, x + maxDist, z + maxDist, [&](Edge& e) {
      if (!LaneGraph::drivable(e, false) || (accept && !accept(e))) return;
      const LaneGraph::EdgeLanes* lanes = graph.edgeLanes[e.id];
      if (!lanes) return;
      if (!std::any_of(lanes->fwd.begin(), lanes->fwd.end(), usable) && !std::any_of(lanes->back.begin(), lanes->back.end(), usable)) return;
      // keep out of the junction areas at both ends
      const double margin = roomy ? 6 : 1;
      const double lo = e.cutA + margin;
      const double hi = e.len - e.cutB - margin;
      if (hi - lo < (roomy ? 8 : 0.5)) return;
      projectOnPolyline(e.pl, x, z, proj);
      const double s = std::max(lo, std::min(hi, proj.s));
      const double d = std::sqrt(proj.d2) + std::fabs(s - proj.s);
      if (d > maxDist) return;
      double score = d;
      const bool named = !want.empty() && (normaliseAddress(e.name) == want || normaliseAddress(e.ref) == want);
      if (!want.empty() && !named) score += 70;
      if (e.cls <= TRUNK_LINK && !named) score += 400;   // nobody lives on the motorway
      if (e.flags & (EF_TUNNEL | EF_BRIDGE)) score += 60;
      // a proper street rather than the yard or car park next to the house
      if (!LaneGraph::drivable(e, true)) score += 150;
      if (!best || score < best->score) best = Best{&e, s, proj.off, score};
    });
    if (best) break;
  }
  if (!best) return std::nullopt;
  const Best& b = *best;
  const LaneGraph::EdgeLanes* lanes = graph.edgeLanes[b.edge->id];
  // the house on the right: it lies left of a→b, so drive b→a (if that way is open)
  std::vector<Lane*> side[2];
  for (int k = 0; k < 2; k++)
    for (Lane* l : (b.off > 0) == (k == 0) ? lanes->back : lanes->fwd)
      if (usable(l)) side[k].push_back(l);
  const std::vector<Lane*>& dir = !side[0].empty() ? side[0] : side[1];
  const Lane* lane = dir[0];
  for (const Lane* l : dir) {
    if (l->idx == 0) {
      lane = l;
      break;
    }
  }
  const double d = lane->dir > 0 ? b.s - lane->s0 : lane->s0 - b.s;
  // (`room`: metres of lane wanted behind the spot – a trailer must not hang back into the junction)
  PolySample smp = newPolySample();
  lanePoint(*lane, std::max(std::min(room, lane->len / 2), std::min(lane->len - std::min(2.0, lane->len / 2), d)), smp);
  // (named after its street, for whoever shows the spot on a map)
  SpawnJson spot;
  spot.name = !b.edge->name.empty() ? b.edge->name : b.edge->ref;
  spot.x = smp.x;
  spot.z = smp.z;
  spot.yaw = std::atan2(-smp.tx, -smp.tz);
  spot.kind = "street";
  return spot;
}

}  // namespace fs
