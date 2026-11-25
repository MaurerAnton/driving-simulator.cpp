// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/world/extrasigns.ts @ v1.7.7
/**
 * The signs a map was built without, and the rules of the signs that hold
 * only at certain times (signs.json.gz, made by tools/build-signs.ts from
 * OpenStreetMap and the map).
 *
 * The posts are the town signs that were missing (where a road leads into a
 * town at a junction, on the small roads into a town), the signs that are
 * mapped in OpenStreetMap beside the road, the plates with a time ("22–6
 * h") – each in the tile it stands in, in the shape the tiles carry their
 * own posts (src/world/tilefmt.ts SignPost).  A post marked `r` takes the
 * place of the plates of the tile's own post that stands there.  The game
 * adds them to each tile as it is loaded.
 *
 * The rules: a speed limit, a road shut to motor vehicles or to lorries –
 * each on one road of the map, in one direction or both, at the times the
 * plate says (src/sim/signtime.ts).  `TimedRules` tells the rule monitor
 * what holds now, by the game's clock.
 *
 * The file belongs to one build of the map (the roads are numbered in it):
 * made for another one, it is left alone.  The game works without it.
 */
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/task.h"
#include "platform/json.h"
#include "sim/signtime.h"
#include "world/tilefmt.h"

namespace fs {

class MapLoader;

/** A post of signs.json.gz: `r` – it takes the place of the plates of the tile's post that stands where it stands. */
struct ExtraPost : SignPost {
  int r = 0;
};
FS_JSON_DECLARE(ExtraPost)

/** A rule set by a sign with a time on it, on one road. */
struct TimedRuleJson {
  /** the road: its number in the map */
  int e = 0;
  /** for which traffic: 1 along the road's course (a → b), −1 against it, 0 both */
  int d = 0;
  /** 'max': the speed limit – 'hgvMax': the speed limit for vehicles over 3.5 t – 'shut': no motor vehicles – 'hgvShut': none over 3.5 t */
  std::string k;
  /** the limit (km/h) of 'max' and 'hgvMax' */
  std::optional<double> v;
  /** when: a time as OpenStreetMap writes it ("Mo-Fr 07:00-17:00"), or 'wet' */
  std::string w;
};
FS_JSON_DECLARE(TimedRuleJson)

struct ExtraSignsJson {
  int v = 0;
  /** the build of the map it was made for (map.json meta.generated) */
  std::string map;
  /** the posts, by the tile they stand in ("ix_iz", src/world/grid.ts) – in the file's order */
  std::vector<std::pair<std::string, std::vector<ExtraPost>>> tiles;
  std::vector<TimedRuleJson> rules;
};
void fromJson(JsonReader& r, ExtraSignsJson& v);

/** The extra signs of a map. */
class ExtraSigns {
 public:
  const std::vector<TimedRuleJson> rules;

  explicit ExtraSigns(const ExtraSignsJson& json);

  /** How many posts there are in all. */
  int posts() const;

  /**
   * Put the posts of tile `key` into its list of posts `signs`: where one is
   * to take the place of a post of the tile, that post gets its plates;
   * the others are added (as copies – a tile loaded again gets them again),
   * standing on `ground(x, z, yRef)` where it says (the road, the pavement
   * or the terrain there; the file's heights are those of the road's middle).
   */
  void mergeInto(const std::string& key, std::vector<SignPost>& signs, const std::function<double(double x, double z, double yRef)>& ground = nullptr) const;

 private:
  std::unordered_map<std::string, std::vector<ExtraPost>> tiles;
};

/**
 * The extra signs of a map (the map loader's `json` and its world,
 * src/world/loader.ts) – null where the map has none, or they were made for
 * another build of it.
 */
Task<std::shared_ptr<ExtraSigns>> loadExtraSigns(std::shared_ptr<MapLoader> map);

/**
 * What the signs with a time on them say now, road by road – by the game's
 * clock (`clock`, the moment of the world in ms since 1970: src/game/clock.ts
 * gameNow), and the weather for those that hold "bei Nässe" (`wet`; dry
 * where the game does not say).
 */
class TimedRules {
 public:
  struct Limit {
    /** km/h */
    double v = 0;
    /** what the sign's panel says ("22–6 h") */
    std::string when;
    bool operator==(const Limit&) const = default;
  };

  /** is the road wet now (rain)? */
  std::function<bool()> wet = [] { return false; };

  TimedRules(const std::vector<TimedRuleJson>& rules, std::function<double()> clock);

  /** How many roads have a rule. */
  int roads() const { return static_cast<int>(byEdge.size()); }

  /**
   * The speed limit a sign with a time on it sets now on road `edge` for
   * traffic going `dir` (+1 along its course) – and what its plate says –,
   * none where none holds now.  It takes the place of the road's own limit
   * (a night-time 30 on a 50 road, a 100 at night where 70 holds by day);
   * `heavy` (over 3.5 t): the limits for lorries count too, the lowest wins.
   */
  std::optional<Limit> limit(int edge, int dir, bool heavy);

  /** Is road `edge` shut now to this vehicle going `dir`, by a sign with a time on it?  What its plate says ("22–6 h") – '' where it is open. */
  std::string shut(int edge, int dir, bool heavy);

 private:
  struct Rule {
    int d = 0;
    std::string k;
    double v = 0;
    /** the original's `when`: a window, or 'wet' */
    TimeWindow when;
    bool wet = false;
    /** what the plate says, for the speed-limit display: "22–6 h", "bei Nässe" */
    std::string text;
  };
  std::unordered_map<int, std::vector<Rule>> byEdge;
  std::function<double()> clock;
  std::optional<SignTime> at;
  double atMs = JS_NAN;

  /** The moment as the signs see it (worked out again once a second of the game's clock has passed, or when it jumped). */
  const SignTime& now();
  bool holdsNow(const Rule& r);
};

}  // namespace fs
