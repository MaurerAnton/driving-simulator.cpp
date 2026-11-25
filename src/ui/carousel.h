// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The main menu in rows over the 3D scene: the vehicle, the town, the place
 * in it, the options of that vehicle, and what to do – each row a strip of
 * cards to swipe along.  The middle of the screen stays free: the scene
 * behind shows the vehicle chosen, standing where the drive will begin
 * (game.ts places it).  The simple menu of before is still there – the
 * settings choose (render in hud.ts showMenu).
 *
 * Origin: src/ui/carousel.ts @ v1.7.7
 */
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "game/address.h"
#include "game/settings.h"
#include "game/vehicles.h"
#include "i18n/index.h"
#include "platform/dom.h"
#include "ui/hud.h"
#include "world/types.h"

namespace fs {

/** What the scene behind the menu is to show: this vehicle, standing here. */
struct MenuPreview {
  VehicleKind kind;
  double x = 0;
  double z = 0;
  double yaw = 0;
  /** the tram: the stop it will start at (it is put on the track there) */
  std::optional<TramStopChoice> stop = std::nullopt;
};

struct CarouselContext {
  Lang lang;
  std::function<std::string(const std::string& key, const TrParams& vars)> t;
  Settings* settings = nullptr;
  std::vector<SpawnJson> spawns;
  /** (null: not known yet) */
  const std::vector<TramStopChoice>* tramStops = nullptr;
  const std::vector<RunwayChoice>* runways = nullptr;
  /** whether the map has a practice yard (the lessons on it) */
  bool hasYard = false;
  /** the language list */
  dom::El language;
  /** a field for an address (calls `pick` with the one chosen) */
  std::function<dom::El(std::function<void(const AddressHit& hit)> pick)> address;
  /** the other pages: settings, controls, log, what's new, about */
  std::vector<std::pair<std::string, std::function<void()>>> tools;
  /** the learning modes (ui/learn.ts): a row of their own – a tap opens one */
  struct Learn {
    std::string title;
    std::string desc;
    std::function<void()> run;
  };
  std::vector<Learn> learn;
  std::string version;
  std::function<void(const StartRequest& req)> start;
  std::function<void(const MenuPreview& p)> preview;
  std::function<void()> redraw;
};

/** The menu of rows. */
dom::El carouselMenu(const CarouselContext& c);

}  // namespace fs
