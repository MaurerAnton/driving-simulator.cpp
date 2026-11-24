// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/queuelabels.ts @ v1.7.7
#include "ui/queuelabels.h"

#include <algorithm>
#include <cmath>

#include "platform/plat.h"

namespace fs {

using dom::El;

/** queues whose end is further than this from the camera get no label (m) */
static const double RANGE = 300;
/** at most this many labels at a time (the nearest) */
static const int MOST = 6;
/** how high over the road the label sits above the last car (m) */
static const double ABOVE = 2.6;
/** seconds between two measurements of the queues */
static const double EVERY = 0.3;
/** the size a label takes, for keeping them apart (CSS pixels) */
static const double LABEL_W = 120;
static const double LABEL_H = 22;

QueueLabels::QueueLabels(const El& parent) {
  layer = dom::el("div");
  layer.className("queuelabels");
  layer.setAttribute("aria-hidden", "true");
  layer.setAttribute("style", "position:absolute;inset:0;overflow:hidden;pointer-events:none");
  // (first: under the interface that lies over the picture)
  parent.prepend(layer);
}

void QueueLabels::hide() {
  for (int i = 0; i < shown; i++) pool[static_cast<size_t>(i)].el.style("display", "none");
  shown = 0;
  last = -JS_INF;
}

QueueLabels::Label& QueueLabels::label(int i) {
  if (i < static_cast<int>(pool.size())) return pool[static_cast<size_t>(i)];
  El el = dom::el("div");
  el.setAttribute("style",
                  "position:absolute;left:0;top:0;padding:2px 8px;border:1px solid rgba(255,255,255,0.35);border-radius:11px;background:rgba(10,16,24,0.4);color:#fff;"
                  "font:600 12px/16px system-ui,-apple-system,\"Segoe UI\",Roboto,sans-serif;font-variant-numeric:tabular-nums;text-shadow:0 0 2px #000;white-space:nowrap;pointer-events:none;will-change:transform;display:none");
  layer.append(el);
  pool.push_back(Label{el, ""});
  return pool.back();
}

std::string QueueLabels::text(const Queue& q, const Lang& lang) {
  std::vector<std::string> parts = {formatUnit(std::max(5.0, jsRound(q.length / 5) * 5), Unit::Meter, lang), tr("queueCars", lang, {{"n", q.cars}})};
  if (std::isfinite(q.wait)) {
    const double s = std::max(1.0, jsRound(q.wait / 5) * 5);
    parts.push_back(s < 100 ? tr("queueWait", lang, {{"s", s}}) : tr("queueWaitMin", lang, {{"n", jsRound(s / 60)}}));
  }
  return join(parts, " · ");
}

void QueueLabels::update(const Camera& camera, double width, double height, double zoom, const std::vector<TrafficCar*>& cars, SignalSystem* signals, const Lang& lang) {
  const double now = plat::nowMs() / 1000;
  if (now - last > EVERY) {
    last = now;
    meter.measure(cars, signals, queues);
  }
  const Vector3& cam = camera.position;
  near.clear();
  for (const Queue& q : queues) {
    const TrafficCar* t = q.tail;
    // (a car that has left the traffic since)
    if (std::find(cars.begin(), cars.end(), t) == cars.end()) continue;
    const double d = std::hypot(t->x - cam.x, t->z - cam.z);
    if (d <= RANGE) near.push_back(Near{&q, d});
  }
  std::stable_sort(near.begin(), near.end(), [](const Near& a, const Near& b) { return a.d < b.d; });
  placed.clear();
  const double w = width / zoom;
  const double h = height / zoom;
  int used = 0;
  for (size_t i = 0; i < near.size() && used < MOST; i++) {
    const Queue& q = *near[i].q;
    const TrafficCar* t = q.tail;
    v.set(t->x, t->y + ABOVE, t->z).project(camera);
    if (v.z > 1 || v.x < -1.05 || v.x > 1.05 || v.y < -1.05 || v.y > 1.05) continue;
    const double x = ((v.x + 1) / 2) * w;
    const double y = ((1 - v.y) / 2) * h;
    bool covered = false;
    for (size_t k = 0; k < placed.size(); k += 2)
      if (std::abs(placed[k] - x) < LABEL_W && std::abs(placed[k + 1] - y) < LABEL_H) covered = true;
    if (covered) continue;
    placed.push_back(x);
    placed.push_back(y);
    Label& l = label(used++);
    const std::string said = QueueLabels::text(q, lang);
    if (said != l.text) {
      l.text = said;
      l.el.textContent(said);
    }
    l.el.style("transform", "translate(" + toFixed(x, 1) + "px, " + toFixed(y, 1) + "px) translate(-50%, -100%)");
    l.el.style("display", "");
  }
  for (int i = used; i < shown; i++) pool[static_cast<size_t>(i)].el.style("display", "none");
  shown = used;
}

}  // namespace fs
