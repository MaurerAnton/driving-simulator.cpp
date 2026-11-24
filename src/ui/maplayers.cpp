// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/maplayers.ts @ v1.7.7
#include "ui/maplayers.h"

#include <cmath>

#include "platform/json.h"
#include "platform/plat.h"

namespace fs {

const char* layerName(LayerId id) {
  switch (id) {
    case LayerId::works: return "works";
    case LayerId::demos: return "demos";
    case LayerId::emergency: return "emergency";
    case LayerId::fuel: return "fuel";
    case LayerId::wash: return "wash";
    case LayerId::photos: return "photos";
  }
  return "";
}

const char* layerKey(LayerId id) {
  switch (id) {
    case LayerId::emergency: return "layerEmergency";
    case LayerId::works: return "layerWorks";
    case LayerId::demos: return "layerDemos";
    case LayerId::fuel: return "layerFuel";
    case LayerId::wash: return "layerWash";
    case LayerId::photos: return "net.layerPhotos";
  }
  return "";
}

bool& LayerFlags::operator[](LayerId id) {
  switch (id) {
    case LayerId::works: return works;
    case LayerId::demos: return demos;
    case LayerId::emergency: return emergency;
    case LayerId::fuel: return fuel;
    case LayerId::wash: return wash;
    case LayerId::photos: return photos;
  }
  return works;
}

bool LayerFlags::operator[](LayerId id) const { return const_cast<LayerFlags&>(*this)[id]; }

ExtraLayers extraLayers;

namespace {
const char* const KEY = "fahrsim.mapLayers";

const char* const EM_COLOURS[4] = {"#2f6fdf", "#e8e8e8", "#d8261c", "#e8e8e8"};
}  // namespace

LayerFlags loadLayers() {
  LayerFlags out;
  const std::optional<std::string> raw = plat::localStorage().getItem(KEY);
  if (raw && !raw->empty()) {
    Json j;
    // (nothing stored, or something else than the flags: all shown)
    if (Json::parse(*raw, j) && j.isObject())
      for (LayerId k : LAYERS)
        if (j.at(layerName(k)).isBool()) out[k] = j.at(layerName(k)).boolean;
  }
  return out;
}

void saveLayers(const LayerFlags& l) {
  // (the members in the order the original's object has them)
  JsonWriter w;
  w.beginObject();
  for (LayerId k : {LayerId::works, LayerId::demos, LayerId::emergency, LayerId::fuel, LayerId::wash, LayerId::photos}) {
    w.key(layerName(k));
    w.boolean(l[k]);
  }
  w.endObject();
  // (a convenience)
  plat::localStorage().setItem(KEY, w.take());
}

void drawLayerIcons(const Ctx2D& g, double W, double H, double scale, const MapScale& sx, const MapScale& sy, const std::vector<MapService>& services, const std::vector<MapEmergency>& emergencies,
                    const LayerFlags& shown, bool blink) {
  g.save();
  g.textAlign("center");
  g.textBaseline("middle");
  // the services only from a town's scale on: else the map would be covered with them
  if (scale >= 0.08) {
    for (const MapService& s : services) {
      if (!(s.k == "fuel" ? shown.fuel : shown.wash)) continue;
      const double x = sx(s.x);
      const double y = sy(s.z);
      if (x < -12 || y < -12 || x > W + 12 || y > H + 12) continue;
      g.fillStyle(s.k == "fuel" ? "#1d7a3a" : "#1f6fb2");
      g.strokeStyle("#ffffff");
      g.lineWidth(1.5);
      g.beginPath();
      g.roundRect(x - 9, y - 9, 18, 18, 4);
      g.fill();
      g.stroke();
      g.fillStyle("#ffffff");
      g.font("700 11px system-ui, sans-serif");
      g.fillText(s.k == "fuel" ? "⛽" : "≋", x, y + 1);
    }
  }
  if (shown.emergency) {
    for (const MapEmergency& e : emergencies) {
      const double x = sx(e.x);
      const double y = sy(e.z);
      if (x < -12 || y < -12 || x > W + 12 || y > H + 12) continue;
      g.fillStyle(e.kind >= 0 && e.kind < 4 ? EM_COLOURS[e.kind] : "#2f6fdf");
      g.strokeStyle(e.call && blink ? "#3b8cff" : "#0d1620");
      g.lineWidth(e.call ? 4 : 2);
      g.beginPath();
      g.arc(x, y, 9, 0, M_PI * 2);
      g.fill();
      g.stroke();
      g.fillStyle(e.kind == 0 ? "#ffffff" : "#c8141c");
      g.font("800 10px system-ui, sans-serif");
      g.fillText(e.kind == 0 ? "P" : e.kind == 2 ? "🔥" : "+", x, y + 1);
    }
  }
  g.restore();
  if (shown.photos && extraLayers.photos) extraLayers.photos->draw(g, W, H, scale, sx, sy);
}

std::optional<LayerIcon> layerIconAt(double px, double py, double scale, const MapScale& sx, const MapScale& sy, const std::vector<MapService>& services, const std::vector<MapEmergency>& emergencies,
                                     const LayerFlags& shown) {
  std::function<void()> extra = shown.photos && extraLayers.photos ? extraLayers.photos->at(px, py, scale, sx, sy) : nullptr;
  if (extra) {
    LayerIcon icon;
    icon.extra = std::move(extra);
    return icon;
  }
  std::optional<LayerIcon> best;
  double bd = 16;
  if (shown.emergency) {
    for (const MapEmergency& e : emergencies) {
      const double d = std::hypot(sx(e.x) - px, sy(e.z) - py);
      if (d < bd) {
        bd = d;
        best = LayerIcon();
        best->em = e;
      }
    }
  }
  if (best || scale < 0.08) return best;
  for (const MapService& s : services) {
    if (!(s.k == "fuel" ? shown.fuel : shown.wash)) continue;
    const double d = std::hypot(sx(s.x) - px, sy(s.z) - py);
    if (d < bd) {
      bd = d;
      best = LayerIcon();
      best->service = s;
    }
  }
  return best;
}

}  // namespace fs
