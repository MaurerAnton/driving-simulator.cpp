// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/takeover.ts @ v1.7.7
/**
 * Any vehicle can be watched and taken over.
 *
 * A tap on a vehicle in the 3D picture chooses it: a car of the traffic, a
 * city bus, a police car, an ambulance, a fire engine, the emergency doctor's
 * car, a tram or a train – the camera then goes with it (game/pointwatch.ts),
 * and touching a driving control (or "Take over") lets the player drive it
 * from where it is.  On the test place ("Every model") a tap on one of the
 * models standing there gets the player into it.
 *
 * The game has no vehicle of its own for each kind of the traffic: a car of
 * the traffic, the police car and the doctor's car are taken over as the
 * car, the ambulance as the van (class C1), the fire engine as the rigid
 * lorry, the city bus as the bus (on its line's way, where it runs one) –
 * `takeKindOf`.
 *
 * A tap is a pointer let go soon and near where it came down; it is tested
 * against the vehicles' boxes along one ray, only when it happens (no cost
 * otherwise), and only while the game says a vehicle may be chosen
 * (`boxes` returns null when not).
 */
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "engine/engine.h"
#include "game/spawnplace.h"
#include "game/vehicles.h"
#include "platform/dom.h"

namespace fs {

/** The kind of the game's vehicles a vehicle of the traffic (by its style) is driven as when taken over. */
VehicleKind takeKindOf(int style);

/**
 * What a tap chose: a car of the traffic (buses and emergency vehicles too) by its id, a tram or train by its id,
 * a model of the test place by its number – `kind` 'car' | 'rail' with `id`, 'show' with `i`.
 */
struct PickTarget {
  std::string kind;
  int id = -1;
  int i = -1;

  bool operator==(const PickTarget&) const = default;
};

/** A vehicle's box (as it stands) and what it is. */
struct PickBox : StandingBox {
  PickTarget what;
};

/** The nearest box along a ray (from o along d) within `reach` metres; null: none. */
std::optional<PickTarget> pickAlong(double ox, double oy, double oz, double dx, double dy, double dz, const std::vector<PickBox>& boxes, double reach = 400);

/** Taps on the 3D picture that choose a vehicle. */
class TapPicker {
 public:
  /**
   * `boxes`: the vehicles that may be chosen now (null: none may – a tap
   * does nothing); `pick`: one was tapped.
   */
  TapPicker(const dom::El& canvas, std::shared_ptr<Camera> camera, std::function<std::optional<std::vector<PickBox>>()> boxes, std::function<void(const PickTarget&)> pick);
  /** (it listens to the canvas for as long as it lives) */
  ~TapPicker();
  TapPicker(const TapPicker&) = delete;
  TapPicker& operator=(const TapPicker&) = delete;

 private:
  struct Down {
    int id = -1;
    double x = 0;
    double y = 0;
    double t = 0;
  };
  Down down;
  Raycaster ray;
  Vector2 ndc;
  dom::El canvas;
  int downListener = 0;
  int upListener = 0;
};

}  // namespace fs
