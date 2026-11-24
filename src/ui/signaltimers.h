// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/signaltimers.ts @ v1.7.7
/**
 * The small timers over the traffic lights: above each head near the player
 * and in view, a half-transparent label with the seconds until its light
 * changes and a dot in the colour that comes next – one for each light the
 * head shows: a head with a green arrow beside it gets a second one for the
 * arrow.  On foot, the pedestrians' lights get theirs instead.
 *
 * A tap or click on a label switches its traffic lights on to their next
 * phase at once – through amber, red for all and red-amber as always, the
 * crossing streams kept apart (SignalSystem.skip).
 *
 * The labels are a handful of buttons, made once and used again, put in
 * place every frame by projecting the head into the picture.  The layer lies
 * under the rest of the interface: a pedal or a menu over a label wins.
 */
#pragma once

#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "engine/engine.h"
#include "platform/dom.h"
#include "sim/signals.h"
#include "world/world.h"

namespace fs {

/** A head as the timers need it (render/actors.ts HeadSpot; world/pedlights.ts PedPole for the pedestrians' heads). */
struct TimerHead {
  double x = 0;
  double y = 0;
  double z = 0;
  /** the way the head faces (0 = north, + = left) */
  double yaw = 0;
  int controller = 0;
  int group = 0;
  /** the green arrow that hangs beside it */
  const Arm* arrow = nullptr;
};

class SignalTimers {
 public:
  /** a tap on a label: the traffic lights it belongs to */
  std::function<void(int controller)> onTap = [](int) {};

  explicit SignalTimers(const dom::El& parent);

  /** The tooltip of the labels (in the player's language). */
  void setHint(const std::string& text);

  /** No labels (another state of the game, a lesson, switched off in the settings). */
  void hide();

  /**
   * Put the labels in place for this frame: the heads within reach of the
   * camera that face it, the nearest first, those that would cover one
   * already placed left out.  `peds`: the pedestrians' heads (on foot) –
   * then only those.  `zoom`: the interface's size (the layer is scaled with it).
   *
   * (`heads` and `peds` are lists of anything that has what a TimerHead has – the
   * renderer's HeadSpot, the pedestrians' PedPole: the original takes them by their shape.)
   */
  template <class Heads, class Peds = std::vector<TimerHead>>
  void update(const Camera& camera, double width, double height, double zoom, const Heads& heads, const Peds* peds, SignalSystem& signals) {
    const Vector3& cam = camera.position;
    near.clear();
    if (peds) consider(cam, *peds, true, signals);
    else consider(cam, heads, false, signals);
    place(camera, width, height, zoom, signals);
  }
  template <class Heads>
  void update(const Camera& camera, double width, double height, double zoom, const Heads& heads, std::nullptr_t, SignalSystem& signals) {
    update<Heads, std::vector<TimerHead>>(camera, width, height, zoom, heads, nullptr, signals);
  }

 private:
  struct Label {
    dom::El el;
    dom::El dot;
    dom::El num;
    std::string text;
    std::string dotClass;
    std::string kind;
    int controller = -1;
  };
  struct Near {
    TimerHead h;
    double d = 0;
    bool ped = false;
  };

  /** heads further than this from the camera get no timer (m) */
  static constexpr double RANGE = 150;

  dom::El layer;
  /** (each label is one object: its button's handler names it) */
  std::vector<std::shared_ptr<Label>> pool;
  int shown = 0;
  Vector3 v;
  std::vector<Near> near;
  std::vector<double> placed;
  /** the label's tooltip (and what a screen reader says) */
  std::string hint;

  static const Arm* arrowOf(const TimerHead& h) { return h.arrow; }
  template <class H>
  static auto arrowOf(const H& h) -> decltype(static_cast<const Arm*>(h.arrow)) {
    return h.arrow;
  }
  static const Arm* arrowOf(...) { return nullptr; }

  template <class List>
  void consider(const Vector3& cam, const List& list, bool ped, const SignalSystem& signals) {
    for (const auto& h : list) {
      const double dx = cam.x - h.x;
      const double dz = cam.z - h.z;
      if (std::abs(dx) > RANGE || std::abs(dz) > RANGE) continue;
      const double d = std::hypot(dx, dz);
      if (d > RANGE || signals.isDark(h.controller)) continue;
      // only heads one sees from the front (their lamps): the facing (−sin yaw, −cos yaw) towards the camera
      if (d > 3 && (-std::sin(h.yaw) * dx - std::cos(h.yaw) * dz) / d < 0.25) continue;
      near.push_back(Near{TimerHead{h.x, h.y, h.z, h.yaw, h.controller, h.group, ped ? nullptr : arrowOf(h)}, d, ped});
    }
  }
  Label& label(int i);
  void place(const Camera& camera, double width, double height, double zoom, SignalSystem& signals);
};

}  // namespace fs
