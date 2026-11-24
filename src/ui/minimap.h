// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/minimap.ts @ v1.7.7
/**
 * The round map in the corner of the screen (drawn by hud.ts drawMap):
 *
 *  - the aerial photographs of the land survey under its roads, as on the
 *    big map (src/reality/aerial.ts; on unless the settings say otherwise) –
 *    also on the car's centre display, whose picture is a texture: the
 *    survey's server sends its pictures for any page (CORS), so the canvas
 *    stays clean;
 *  - dragging it looks round: its middle moves away from the vehicle, and
 *    comes back to it a few seconds after the finger has let go – or at once
 *    with the button that appears meanwhile.  A plain tap opens the big map,
 *    full screen (as its lower button does; the real photographs of the
 *    place are in the pause menu);
 *  - dragging its rim moves the whole map to another place on the screen,
 *    which is kept (one place for the keyboard layout, one for the touch
 *    layout of the HUD); a double tap on the rim puts it back.
 *
 * 2.0: the aerial photographs are those of the state the map lies in (reality/aerial.h
 * aerialStateTiles), so a region's corner map has them anywhere in Germany.
 */
#pragma once

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "platform/canvas.h"
#include "platform/dom.h"
#include "ui/maphooks.h"

namespace fs {

struct MiniMapHost {
  /** the label of the button that brings the map back to the vehicle */
  std::function<std::string()> backLabel;
  /** the label of the rim (how to move the map) */
  std::function<std::string()> moveLabel;
  /** may the aerial photographs be fetched? */
  std::function<bool()> photos;
  /** where the map lies in the survey's coordinates (world x = east − originE, z = originN − north) and what it covers */
  struct Meta {
    double originE = 0;
    double originN = 0;
    double minX = 0;
    double maxX = 0;
    double minZ = 0;
    double maxZ = 0;
  } meta;
};

class MiniMap {
 public:
  /** (`box`: the map's element in the HUD; `canvas`: its picture – the object lives as long as they do) */
  MiniMap(const dom::El& box, const Canvas& canvas, MiniMapHost host);
  MiniMap(const dom::El& box, const dom::El& canvas, MiniMapHost host) : MiniMap(box, Canvas(canvas), std::move(host)) {}
  MiniMap(const MiniMap&) = delete;
  MiniMap& operator=(const MiniMap&) = delete;

  /**
   * The middle of the map for a picture: the vehicle at (x, z) – or, while
   * the player looks round, where the map was dragged to (it drifts back to
   * the vehicle a while after).  `own`: the picture is the corner map's
   * (its heading and scale turn drags into metres).
   */
  std::array<double, 2> centre(double x, double z, double yaw, double scale, bool own);

  /**
   * The aerial photographs round (cx, cz) within `range` metres, drawn into
   * the map's world coordinates (the transform of the roads) at `pxPerMetre`
   * – if the settings allow them.  True when they are drawn (the roads over
   * them are then only hinted at).
   */
  bool photos(const Ctx2D& g, double cx, double cz, double range, double pxPerMetre);

 private:
  struct Place {
    double fx = 0;
    double fy = 0;
  };
  struct Drag {
    int id = 0;
    bool move = false;
    double x = 0;
    double y = 0;
    double moved = 0;
    double left = 0;
    double top = 0;
  };

  MiniMapHost host;
  dom::El box;
  Canvas canvas;
  /** looking round: how far the middle is from the vehicle (metres east, south) */
  double ox = 0;
  double oz = 0;
  double lookedAt = -1e9;
  double lastCentre = 0;
  /** the heading and scale of the last picture of the corner map: a drag is turned into metres by them */
  double yaw = 0;
  double scale = 0.42;
  std::unique_ptr<AerialCache> photosCache;
  dom::El backBtn;
  std::optional<Drag> drag;
  bool eatClick = false;
  std::string layout;
  /** `Record<string, Place>`, in the order the places were put in (as JSON.stringify writes them) */
  std::vector<std::pair<std::string, Place>> places;
  double lastRimTap = 0;

  static std::vector<std::pair<std::string, Place>> loadPlaces();
  Place* placeOf(const std::string& layout);
  /** Which layout of the HUD the map is in (it has a place of its own in each). */
  std::string layoutNow() const;
  void applyPlace();
  /** The room the map has (its box, the HUD), in the HUD's own pixels (the interface may be zoomed: settings uiScale). */
  std::array<double, 2> room() const;
  /** How many of the HUD's own pixels a pixel of the screen is. */
  double ratio() const;
  /** Put the map's upper left corner here (the HUD's pixels), within the screen. */
  void putAt(double left, double top);
  bool onRim(const dom::Event& e) const;
  void down(dom::Event& e);
  void move(dom::Event& e);
  void up(dom::Event& e);
  void save();
};

}  // namespace fs
