// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/queuelabels.ts @ v1.7.7
/**
 * The small labels over the queues in the traffic: over the last car of a
 * queue near the player and in view, a half-transparent label with how long
 * the queue is, how many cars stand in it and – where that can be told –
 * about how long the last one waits (src/sim/queues.ts measures them).
 *
 * As the timers over the traffic lights (ui/signaltimers.ts): a handful of
 * elements, made once and used again, put in place every frame by
 * projecting the end of the queue into the picture.  The queues themselves
 * are measured a few times a second only.  The labels take no pointer: the
 * picture under them is still turned and tapped as before.
 */
#pragma once

#include <string>
#include <vector>

#include "core/js.h"
#include "engine/engine.h"
#include "i18n/index.h"
#include "platform/dom.h"
#include "sim/queues.h"
#include "sim/signals.h"
#include "sim/traffic.h"

namespace fs {

class QueueLabels {
 public:
  explicit QueueLabels(const dom::El& parent);

  /** No labels (another state of the game, a lesson or a test, switched off in the settings). */
  void hide();

  /** What a label says: "120 m · 18 cars · about 45 s". */
  static std::string text(const Queue& q, const Lang& lang);

  /**
   * Put the labels in place for this frame: the queues whose end is within
   * reach of the camera and in front of it, the nearest first, those that
   * would cover one already placed left out.  `zoom`: the interface's size
   * (the layer is scaled with it).
   */
  void update(const Camera& camera, double width, double height, double zoom, const std::vector<TrafficCar*>& cars, SignalSystem* signals, const Lang& lang);

 private:
  struct Label {
    dom::El el;
    std::string text;
  };
  struct Near {
    const Queue* q = nullptr;
    double d = 0;
  };

  dom::El layer;
  std::vector<Label> pool;
  int shown = 0;
  QueueMeter meter;
  std::vector<Queue> queues;
  double last = -JS_INF;
  Vector3 v;
  std::vector<Near> near;
  std::vector<double> placed;

  Label& label(int i);
};

}  // namespace fs
