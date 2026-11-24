// SPDX-License-Identifier: AGPL-3.0-only
// 2.0: the big map's two looks, and its preferences (ui/mapstyle.h)
#include "ui/mapstyle.h"

#include "platform/json.h"
#include "platform/plat.h"

namespace fs {

RoadKind roadKindOfClass(int cls) {
  switch (cls) {
    case 0: case 1: case 2: case 3: return RoadKind::motorway;   // motorways and trunk roads, with their ramps
    case 4: case 5: return RoadKind::primary;
    case 6: case 7: return RoadKind::secondary;
    case 8: case 9: return RoadKind::tertiary;
    case 10: case 11: case 12: return RoadKind::minor;
    default: return RoadKind::service;
  }
}

RoadKind roadKindOfTier(double tier) {
  // (CLASS_TIER: motorway 7, trunk 6, primary 5, its link 4.5, secondary 4, its link 3.5, tertiary 3, …)
  if (tier >= 6) return RoadKind::motorway;
  if (tier >= 4.5) return RoadKind::primary;
  if (tier >= 3.5) return RoadKind::secondary;
  if (tier >= 2.5) return RoadKind::tertiary;
  if (tier >= 1) return RoadKind::minor;
  return RoadKind::service;
}

namespace {

// the dark map: 1.7.7's colours (bigmap.ts), the kinds of road between its four shades
const MapStyle DARK = {
    "dark",
    "#16202a",
    "#1c2a36",
    "#131c25",
    "#1f2b36",
    "#3b4d5d",
    "#5a6f82",
    "rgba(170, 180, 189, 0.55)",
    "#38b6ff",
    "rgba(56, 182, 255, 0.16)",
    "#ffcc00",
    "rgba(255, 204, 0, 0.14)",
    "rgba(255, 255, 255, 0.22)",
    "#f1f3f5",
    {"#55606b", "#aab4bd", "#d5dbe0", "#f1f3f5", "#f6dc9c", "#f5b942"},
    {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    "#ffffff",
    "#d5dde4",
    "rgba(12, 18, 24, 0.85)",
};

// the light map: as the street maps one knows (land in a pale beige, water blue, main roads in colour, every road with a rim)
const MapStyle LIGHT = {
    "light",
    "#aad3df",
    "#f2efe9",
    "#aad3df",
    "#f2efe9",
    "#a39aa8",
    "#7fb2c4",
    "rgba(90, 80, 100, 0.6)",
    "#2a7fd4",
    "rgba(42, 127, 212, 0.10)",
    "#d38b00",
    "rgba(255, 190, 40, 0.12)",
    "rgba(42, 127, 212, 0.22)",
    "#1d2733",
    {"#ffffff", "#ffffff", "#ffffff", "#f7fabf", "#fcd6a4", "#e892a2"},
    {"#d0ccc4", "#bdb8b0", "#a8a39b", "#b5ba7a", "#c9a36c", "#c2607a"},
    "#1b1b1b",
    "#3d3d3d",
    "rgba(255, 255, 255, 0.92)",
};

const char* const KEY = "fahrsim2.mapPrefs";

}  // namespace

const MapStyle& mapStyleOf(const std::string& name) { return name == "light" ? LIGHT : DARK; }

MapPrefs loadMapPrefs() {
  MapPrefs out;
  const std::optional<std::string> raw = plat::localStorage().getItem(KEY);
  if (raw && !raw->empty()) {
    Json j;
    // (nothing stored, or something else: the defaults)
    if (Json::parse(*raw, j) && j.isObject()) {
      if (j.at("style").isString() && (j.at("style").str == "dark" || j.at("style").str == "light")) out.style = j.at("style").str;
      if (j.at("online").isBool()) out.online = j.at("online").boolean;
    }
  }
  return out;
}

void saveMapPrefs(const MapPrefs& p) {
  JsonWriter w;
  w.beginObject();
  w.key("style");
  w.string(p.style);
  w.key("online");
  w.boolean(p.online);
  w.endObject();
  plat::localStorage().setItem(KEY, w.take());
}

}  // namespace fs
