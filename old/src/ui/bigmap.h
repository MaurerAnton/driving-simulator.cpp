// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The full-screen map: the whole road network north up, where the car is,
 * where it has been, and – in a free drive – a way to put it anywhere: tap a
 * road, then "put the car here".  A demonstration drive is planned here:
 * places by a tap on the map or by their address.
 *
 * Drag to move the map, pinch or use the wheel to zoom.  Everything works
 * with a finger, a mouse or the keyboard (arrows, + and -).  The picture is
 * drawn into a canvas the platform paints (as the mini map does).
 *
 * Origin: src/ui/bigmap.ts (MaurerAnton/driving-simulator @ ff37c5d, v0.7.0).
 */
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "game/address.h"
#include "game/track.h"
#include "reality/aerial.h"
#include "ui/hud.h"

namespace fs {

class World;
struct AddressPicker;   // defined in ui/addresspicker.h (kept out: it needs HudNode)

/** What the map asks of the game (the original's BigMapHost). */
struct BigMapHost {
  World* world = nullptr;
  /** the language of the texts */
  std::function<std::string()> lang;
  /** the car: position and yaw (0 = north, positive = to the left) */
  std::function<void(double& x, double& z, double& yaw)> car;
  /** where the car has been */
  DrivenPath* path = nullptr;
  /** the place on a road next to a point of the map where the car could stand, or none */
  std::function<SpawnJson(double x, double z)> spotAt;
  /** may the car be put somewhere else (a free drive)? */
  std::function<bool()> canTeleport;
  std::function<void(const SpawnJson&)> teleport;
  /** the address list, for adding a place by its address */
  std::function<AddressIndex*()> loadAddresses;
  /** the place on the road in front of an address, or none */
  std::function<SpawnJson(const AddressHit&, bool& ok)> spotOf;
  /** a demonstration drive: the car drives by itself to these places, in this order */
  std::function<void(const std::vector<SpawnJson>&)> startDemo;
  /** latitude and longitude of a point of the map */
  std::function<std::pair<double, double>(double x, double z)> latLon;
  /** aerial photographs under the roads: has the player switched them on? */
  std::function<bool()> aerial;
  std::function<void(bool)> setAerial;
  std::function<void()> close;
  /** the window the map is drawn into (CSS pixels) */
  std::function<void(int& w, int& h)> windowSize;
  /**
   * The panel is ready / goes away: the game hands it to the platform (the
   * Hud's overlay slot) and takes its own tree back again.
   */
  std::function<void(std::unique_ptr<HudNode>)> onShow;
  std::function<void()> onHide;
  /** the platform's HTTP (the pictures of the aerial photographs) */
  HttpBackend* http = nullptr;
};

/**
 * The map itself.  `open` builds the panel; the picture is produced by
 * `paint` into the canvas the platform shows; `key`/`drag`/`tap`/`wheel`
 * carry what the fingers and keys do.
 */
class BigMap {
 public:
  explicit BigMap(BigMapHost host);
  ~BigMap();   // out of line: AddressPicker is an incomplete type here

  bool isOpen() const { return open_; }

  /** `closeUp`: a close-up of where the car is (for comparing with an aerial photograph). */
  void open(bool closeUp = false);
  /** Open the panel for planning a demonstration drive (the map must be open). */
  void plan();
  void close();

  // -- what the platform reports --------------------------------------------
  /** one key of the map's own (arrows move the map, + and - zoom). */
  void key(const std::string& code);
  /** the pointer went down / moved / lifted at (x, y) of the canvas. */
  void down(double x, double y);
  void drag(double x, double y);
  void up(double x, double y);
  void wheel(double dy, double x, double y);

  /** Draw the map into `into` (sized by the caller: the window). */
  void paint(Canvas2D& into);
  /** The answers of the aerial photographs have arrived: decode, redraw. */
  void poll() { photos_.poll(); }

 private:
  BigMapHost host_;
  bool open_ = false;
  double cx_ = 0, cz_ = 0;
  double scale_ = 0.25;
  bool showPath_ = true;
  bool hasSpot_ = false;
  SpawnJson spot_;
  /** the places of a demonstration drive being planned (kept while the map is closed) */
  std::vector<SpawnJson> stops_;
  /** plan panel open? */
  bool planOpen_ = false;
  bool planDirty_ = false;
  int moved_ = 0;
  bool dragging_ = false;
  double lastX_ = 0, lastY_ = 0;
  AerialCache photos_;
  std::unique_ptr<HudNode> root_;
  /** the plan panel inside the tree the platform holds */
  HudNode* planNode_ = nullptr;

  void fillPlan();
  void choose(double px, double py);
  void zoom(double factor, double px, double py);
  /** the address picker of the plan panel (rebuilt with the list) */
  std::unique_ptr<AddressPicker> picker_;
  void rebuildPicker();
};

}  // namespace fs
