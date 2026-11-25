// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The HUD of the SDL window (ui/sdlhud): what the Hud was given and shows is painted into
 * one picture – the street name (or the notice in its place), the speed limit, the speed
 * and the gear, the tip, the score and the corner map – and painted again only when
 * something shown changes.
 *
 *   tools/t.sh sdlhud_test [folder]   – with a folder: the pictures as PNG files there
 */
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

#include "platform/canvas.h"
#include "platform/dom.h"
#include "platform/native/canvas_native.h"
#include "ui/hud.h"
#include "ui/sdlhud.h"
#include "world/world.h"

using namespace fs;

/** the picture (premultiplied) as a PNG file */
static void save(const SdlHud::Picture& p, const std::string& path) {
  const Canvas c(p.width, p.height);
  ImageData d;
  d.width = p.width;
  d.height = p.height;
  d.data.assign(p.rgba, p.rgba + static_cast<size_t>(p.width) * static_cast<size_t>(p.height) * 4);
  for (size_t i = 0; i < d.data.size(); i += 4) {
    const int a = d.data[i + 3];
    if (a > 0 && a < 255)
      for (int k = 0; k < 3; k++) d.data[i + static_cast<size_t>(k)] = static_cast<uint8_t>(std::min(255, d.data[i + static_cast<size_t>(k)] * 255 / a));
  }
  c.getContext2D().putImageData(d, 0, 0);
  assert(canvasNative::savePng(c, path));
  std::printf("  %s\n", path.c_str());
}

static const uint8_t* px(const SdlHud::Picture& p, int x, int y) { return p.rgba + (static_cast<size_t>(y) * static_cast<size_t>(p.width) + static_cast<size_t>(x)) * 4; }

int main(int argc, char** argv) {
  const std::string out = argc > 1 ? argv[1] : "";
  WorldJson json;
  json.version = 1;
  json.meta.name = "Baunatal";
  json.meta.minX = -500;
  json.meta.minZ = -500;
  json.meta.maxX = 500;
  json.meta.maxZ = 500;
  json.meta.tileSize = 500;
  World world(std::move(json));
  Settings settings;
  settings.lang = "de";
  const dom::El root = dom::el("div");
  dom::body().append(root);
  Hud hud(root, settings, world);
  SdlHud sdl(hud);

  // nothing driven yet: nothing over the frame
  SdlHud::Picture p = sdl.paint(1280, 720);
  assert(!p.rgba);

  HudState s;
  s.speedKmh = 47.4;
  s.gear = "3";
  s.rpm = 2600;
  s.limit = 50;
  s.navText = "Kirchbaunaer Straße";
  s.navSub = "Baunatal";
  s.navArrow = -1;
  s.tip = "Blinker links setzen";
  s.faults = 1;
  s.points = 3;
  hud.setHudVisible(true);
  hud.update(s, 1.0);
  p = sdl.paint(1280, 720);
  assert(p.rgba && p.width == 1280 && p.height == 720);
  // the street-name block top left is the page's dark panel; the middle of the screen stays free
  assert(px(p, 30, 30)[3] > 200);
  assert(px(p, 640, 360)[3] == 0);
  // the speed limit top right: the red ring
  {
    const uint8_t* r = px(p, 1280 - 14 - 39 - 35, 14 + 39);
    assert(r[0] > 180 && r[1] < 60 && r[3] > 240);
  }
  // the strip at the bottom middle
  assert(px(p, 640, 720 - 14 - 26)[3] > 200);
  if (!out.empty()) save(p, out + "/sdlhud_drive.png");

  // the same again: not painted anew
  const uint64_t v = p.version;
  hud.update(s, 1.05);   // (too soon for the page to be touched, or the map drawn again)
  assert(sdl.paint(1280, 720).version == v);

  // a fault takes the street name's place (red: serious)
  Violation fault;
  fault.id = "redLight";
  fault.severity = "major";
  hud.toast(fault);
  s.speedKmh = 52;
  hud.update(s, 1.4);
  p = sdl.paint(1280, 720);
  assert(p.version != v);
  {
    const uint8_t* r = px(p, 20, 20);
    assert(r[0] > 150 && r[1] < 60 && r[3] > 230);
  }
  if (!out.empty()) save(p, out + "/sdlhud_fault.png");

  // a smaller window: the picture follows its size
  p = sdl.paint(640, 360);
  assert(p.width == 640 && p.height == 360);
  if (!out.empty()) save(p, out + "/sdlhud_small.png");

  // the HUD hidden (a menu): nothing
  hud.setHudVisible(false);
  assert(!sdl.paint(1280, 720).rgba);
  std::printf("sdlhud_test: ok\n");
  return 0;
}
