// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/maplayers.ts @ v1.7.7
/**
 * The big map's layers the player may hide or show: the roadworks, the
 * demonstrations, the emergency vehicles, the fuel stations, the car
 * washes (the trams and buses have their own switch) – remembered in this
 * browser – and how the icons of the services and the emergency vehicles
 * are drawn and found under a finger.
 */
#pragma once

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "platform/canvas.h"

namespace fs {

/** `'works' | 'demos' | 'emergency' | 'fuel' | 'wash' | 'photos'` */
enum class LayerId { works, demos, emergency, fuel, wash, photos };
inline constexpr std::array<LayerId, 6> LAYERS = {LayerId::emergency, LayerId::works, LayerId::demos, LayerId::fuel, LayerId::wash, LayerId::photos};
/** the layer's name as the original writes it ('works' …): the key it is stored under */
const char* layerName(LayerId id);
/** `LAYER_KEYS[id]`: the text of its switch */
const char* layerKey(LayerId id);

/** `Record<LayerId, boolean>` */
struct LayerFlags {
  bool works = true;
  bool demos = true;
  bool emergency = true;
  bool fuel = true;
  bool wash = true;
  bool photos = true;
  bool& operator[](LayerId id);
  bool operator[](LayerId id) const;
};

/** world → screen */
using MapScale = std::function<double(double)>;

/**
 * Layers brought by other parts of the game: the players' photos of a server
 * (net/online.ts) – drawn after the icons, found under a finger before them –
 * and a place to be picked: while `pick` is set, a tap on the map hands its
 * place over instead of anything else (ui/bigmap.ts).
 */
struct ExtraLayer {
  std::function<void(const Ctx2D& g, double W, double H, double scale, const MapScale& sx, const MapScale& sy)> draw;
  /** what a tap at (px, py) opens – null: nothing there */
  std::function<std::function<void()>(double px, double py, double scale, const MapScale& sx, const MapScale& sy)> at;
};
struct ExtraLayers {
  ExtraLayer* photos = nullptr;
  std::function<void(double x, double z)> pick = nullptr;
};
extern ExtraLayers extraLayers;

/** Which layers are shown (all, unless the player hid some). */
LayerFlags loadLayers();

void saveLayers(const LayerFlags& l);

/** A fuel station or car wash on the map. */
struct MapService {
  /** 'fuel' | 'wash' */
  std::string k;
  std::string name;
  std::string brand;
  double x = 0;
  double z = 0;
  std::string osm;
  /** (empty: the file does not say) */
  std::vector<std::string> fuels;
};

/** An emergency vehicle of the traffic on the map. */
struct MapEmergency {
  int id = 0;
  /** 0 police, 1 ambulance, 2 fire engine, 3 emergency doctor */
  int kind = 0;
  bool call = false;
  double x = 0;
  double z = 0;
};

/** Draw the services and the emergency vehicles in view (sx, sy: world → screen). */
void drawLayerIcons(const Ctx2D& g, double W, double H, double scale, const MapScale& sx, const MapScale& sy, const std::vector<MapService>& services, const std::vector<MapEmergency>& emergencies,
                    const LayerFlags& shown, bool blink);

/** `{ em } | { service } | { extra }`: exactly one of them is set */
struct LayerIcon {
  std::optional<MapEmergency> em;
  std::optional<MapService> service;
  std::function<void()> extra;
};

/** What icon is under a point of the map (null: none) – the emergency vehicles first. */
std::optional<LayerIcon> layerIconAt(double px, double py, double scale, const MapScale& sx, const MapScale& sy, const std::vector<MapService>& services, const std::vector<MapEmergency>& emergencies,
                                     const LayerFlags& shown);

}  // namespace fs
