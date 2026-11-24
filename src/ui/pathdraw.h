// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/pathdraw.ts @ v1.7.7
/**
 * Drawing the way on the big map: the player draws with a finger or the
 * mouse how the car is to drive, and the line is laid onto the roads – the
 * car then drives exactly that way by itself (or the player drives it and is
 * shown the way), as along a planned tour (game.ts startTrip).
 *
 * Laying the line onto the roads (`matchStroke`) is what navigation devices
 * call map matching.  The line is sampled every few metres; for each sample
 * the roads near it are the candidates – each in the direction it may be
 * driven and that agrees with the direction of drawing; then, from the first
 * sample to the last, the chain of candidates is chosen whose way along the
 * roads is closest to the line drawn (a Viterbi search): each candidate costs
 * its distance from the line and its angle to it, each step from one sample
 * to the next costs how much longer the way by the road is than the line
 * between them.  Two samples on different roads are joined by the shortest
 * legal way between them – along the turns the junctions allow, from the
 * route planner's network (src/world/routegraph.ts), U-turns only where
 * nothing else leads there.
 */
#pragma once

#include <array>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "i18n/index.h"
#include "platform/canvas.h"
#include "platform/dom.h"
#include "world/netindex.h"
#include "world/routegraph.h"
#include "world/world.h"

namespace fs {

struct TourPreview;

/** The roads as the matching sees them. */
struct RoadSource {
  /** the route planner's network: ways, their lengths and the turns between them (it stays where it is while the source is used) */
  const RouteGraph* g = nullptr;
  /** visit the roads (their numbers: way = road * 2 + direction) that come near a rectangle */
  std::function<void(double minX, double minZ, double maxX, double maxZ, const std::function<void(int road)>& visit)> roadsIn;
  /** the course of a road from its junction a to b: x, z, x, z, … (null: not known) */
  std::function<std::optional<std::vector<double>>(int road)> course;
  /** may this way be used (part of the network one can drive around in)? – missing: every way that can be driven */
  std::function<bool(int way)> usable = nullptr;
};

/** The roads of the whole map (a map in pieces: NetIndex), else the roads loaded (a map in one piece). */
RoadSource roadSource(const RouteGraph& g, const NetIndex* net, const World& world);

/** where a way begins or ends, and which way the road runs there (yaw: 0 north, positive to the left) */
struct PathEnd {
  double x = 0;
  double z = 0;
  double yaw = 0;
};

/** A drawn line laid onto the roads. */
struct MatchedPath {
  /** the ways to drive, in order – the first is the one the drawing begins on */
  std::vector<int> ways;
  /** its line along the roads, x, z, … from where the drawing begins to where it ends */
  std::vector<float> line;
  double metres = 0;
  /** where it begins and ends, and which way the road runs there (yaw: 0 north, positive to the left) */
  PathEnd start;
  PathEnd end;
};

struct MatchOptions {
  /** metres between the samples of the line (default: 15, more on a long line) */
  std::optional<double> step;
  /** how far from the line a road may be to count (m, default 35) */
  std::optional<double> radius;
};

/**
 * Lay a line drawn on the map (x, z, x, z, … in the order it was drawn) onto
 * the roads: the ways to drive, and the line along them.  Null when no road
 * is near enough to it.
 */
std::optional<MatchedPath> matchStroke(const std::vector<double>& points, const RoadSource& roads, const MatchOptions& opt = {});

/** A matched way as the game drives a planned tour along it (startTrip): from its first way on. */
TourPreview previewOf(const MatchedPath& m);

// ---------------------------------------------------------------- drawing on the map

struct PathDrawHost {
  std::function<std::string()> lang;
  /** the roads (null: those of the whole map are not read yet) */
  std::function<const RoadSource*()> roads;
  /** a point of the screen (client px) on the map */
  std::function<std::array<double, 2>(double clientX, double clientY)> toWorld;
  /** move the map by screen pixels, zoom it round a point of the screen */
  std::function<void(double dx, double dy)> pan;
  std::function<void(double factor, double clientX, double clientY)> zoom;
  std::function<void()> redraw;
  /** drawing begins or ends: the panel for planning makes room, and comes back */
  std::function<void(bool on)> mode;
  /** drive the way: by itself, or the player drives and is shown the way */
  std::function<void(const TourPreview& way, bool self)> drive;
};

/** The drawing mode of the big map: the strokes, the way they make, and the buttons for it. */
class PathDrawer {
 public:
  explicit PathDrawer(PathDrawHost host);
  PathDrawer(const PathDrawer&) = delete;
  PathDrawer& operator=(const PathDrawer&) = delete;

  /** (the original's getter `drawing`) */
  bool drawing() const { return active; }

  /** The map opened: its window (`root`, whose canvas is drawn on) and the column of its controls (`ui`, the banner goes after `after`). */
  void attach(const dom::El& root, const dom::El& canvas, const dom::El& ui, const dom::El& after);

  void detach();

  /** Drawing on or off. */
  void setActive(bool on);

  void clear();

  /** The part of the panel for planning: draw, and drive what was drawn. */
  dom::El section();

  /** Draw the strokes and the way they make (in screen pixels). */
  void draw(const Ctx2D& g, double W, double H, double s, double cx, double cz);

 private:
  PathDrawHost host;
  /** the strokes drawn (world x, z, …), in their order – kept while the map is closed */
  std::vector<std::vector<double>> strokes;
  std::optional<std::vector<double>> cur;
  std::optional<MatchedPath> match;
  std::string note;
  bool active = false;
  dom::El canvas;
  dom::El root;
  dom::El banner;
  struct Finger {
    double x = 0;
    double y = 0;
  };
  /** (in the order the fingers came down, as the original's Map keeps them) */
  std::vector<std::pair<int, Finger>> fingers;
  double pinch = 0;
  Finger lastPx;
  /** the listeners on `root` (pointerdown, pointermove, pointerup, pointercancel), to take them away again */
  int listeners[4] = {0, 0, 0, 0};

  std::string t(const std::string& key, const TrParams& p = {}) const;
  Finger* finger(int id);
  void down(dom::Event& e);
  void move(dom::Event& e);
  void up(dom::Event& e);
  void finish();
  void rematch();
  void showBanner();
  void drive(bool self);
};

}  // namespace fs
