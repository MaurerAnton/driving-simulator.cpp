// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Traffic signs.  The official sign graphics (public-domain SVGs, bundled in
 * public/signs/signs.json) are rasterised into one texture atlas on demand;
 * town signs carry generated text.  Signs without a graphic fall back to a
 * simple procedural rendering, so the game never shows a blank plate.
 *
 * Origin: src/render/signs.ts (MaurerAnton/driving-simulator @ 5b6ddc7,
 * v0.1.0).  The atlas is a software canvas (src/platform/canvas2d.h); the
 * SVG path comes from the platform (SignSource), which is also where the
 * browser would decode it.
 */
#pragma once

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "engine/scene.h"
#include "engine/webgl.h"
#include "platform/canvas2d.h"
#include "render/tiles.h"
#include "world/tilefmt.h"

namespace fs {

/** Plate size in metres for a sign code. */
std::pair<double, double> signSize(const std::string& code, double aspect, bool big);

/** Where the sign graphics come from (the platform fetches them). */
class SignSource {
 public:
  virtual ~SignSource() = default;
  /** Read signs.json and manifest.json from `baseUrl`. */
  virtual void load(const std::string& baseUrl) = 0;
  /** The SVG markup for a sign code, or an empty string. */
  virtual std::string svg(const std::string& base) = 0;
  /** The intrinsic size of that graphic, or {0, 0}. */
  virtual std::pair<int, int> size(const std::string& base) = 0;
  /** Decode `svg` and draw it into `canvas` at (x, y, w, h). */
  virtual void rasterise(const std::string& svg, Canvas2D& canvas, double x, double y, double w,
                         double h) = 0;
};

class SignRenderer {
 public:
  SignRenderer(const std::string& baseUrl, SignSource& source);

  /** Load the graphics (the platform reads signs.json / manifest.json). */
  void init();
  /**
   * Call once per frame: after new signs were drawn, the atlas goes to the
   * renderer's texture (three.js: texture.needsUpdate = true).
   */
  void flush(class WebGLRenderer& renderer);

  void add(LoadedTile& tile);
  void remove(const std::string& key);

  /** Codes whose graphic was not available (the procedural fallback was used). */
  std::set<std::string> missing;

  /** The atlas image (RGBA), for tests and for the renderer's texture. */
  const std::vector<uint8_t>& atlasPixels() const { return canvas_.pixels(); }
  int atlasWidth() const { return canvas_.width(); }
  int atlasHeight() const { return canvas_.height(); }

 private:
  struct Slot {
    /** uv rectangle of the drawable area */
    double u0 = 0, v0 = 0, u1 = 0, v1 = 0;
    /** width / height of the plate */
    double aspect = 1;
  };

  std::string baseUrl_;
  SignSource& source_;
  Canvas2D canvas_;
  double aspectOf(const std::string& code) const;
  Slot& slot(const std::string& code);
  void draw(const std::string& code, double x, double y, double w, double h);
  /** Simple procedural rendering of the most important signs. */
  void fallback(const std::string& code, double x, double y, double w, double h);

  std::map<std::string, Slot> slots_;
  int next_ = 1;  // slot 0 is the plain grey back side
  std::map<std::string, std::unique_ptr<Group>> perTile_;
  bool dirty_ = false;
};

}  // namespace fs
