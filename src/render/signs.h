// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Traffic signs.  The official sign graphics (public-domain SVGs, bundled in
 * public/signs/signs.json) are rasterised into one texture atlas on demand;
 * town signs carry generated text.  Signs without a graphic fall back to a
 * simple procedural rendering, so the game never shows a blank plate.
 *
 * Origin: src/render/signs.ts @ v1.7.7
 */
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/task.h"
#include "engine/engine.h"
#include "platform/canvas.h"
#include "world/tilefmt.h"

namespace fs {

struct LoadedTile;

/** Plate size in metres for a sign code. */
std::pair<double, double> signSize(const std::string& code, double aspect, bool big);

class SignRenderer {
 public:
  TexturePtr texture;
  MaterialPtr material;
  MaterialPtr backMaterial;
  /** the signs that had no graphic and are not drawn by signart (a Set in the original: each once, in the order they were found) */
  std::vector<std::string> missing;
  /** a post of the map's tiles as it is to be drawn – without what is drawn elsewhere instead (src/sim/levelcrossing); none: not at all */
  std::function<std::optional<SignPost>(const SignPost& p)> editPost;

  SignRenderer(const std::string& baseUrl, double maxAnisotropy);
  ~SignRenderer();
  SignRenderer(const SignRenderer&) = delete;
  SignRenderer& operator=(const SignRenderer&) = delete;

  Task<void> init();
  /** Call once per frame: uploads the atlas after new signs were drawn. */
  void flush();
  void add(LoadedTile& tile);
  /**
   * The posts and plates of these signs as one group (positions relative to
   * cx, yBase, cz for float precision) – for a tile, or for signs that stand
   * only today (sim/roadevents).  (The original also hands back what to
   * dispose of when the group goes – the plates' geometry and the mesh of the
   * posts: here they go with the last reference to the group.)
   */
  std::shared_ptr<Group> build(const std::vector<SignPost>& posts, double cx, double yBase, double cz);
  void remove(const LoadedTile& tile);
  /**
   * A group of posts made by `build` is gone: the signs it showed may be
   * drawn over in the atlas once no other group shows them (a group never
   * released keeps its signs there).
   */
  void release(const Object3D& group);

 private:
  // (cells of 192 px: a hundred of them in the atlas, a wide plate takes two – reused once no tile shows their sign)
  static constexpr int CELL = 192;
  static constexpr int COLS = 10;
  static constexpr int ROWS = 10;
  static constexpr int PAD = 5;

  struct Slot {
    /** uv rectangle of the drawable area */
    double u0 = 0;
    double v0 = 0;
    double u1 = 0;
    double v1 = 0;
    /** width / height of the plate */
    double aspect = 1;
    /** its first cell in the atlas (-1: none – the atlas was full), and whether it takes the next one too */
    int cell = -1;
    bool wide = false;
    /** how many groups of posts show it now, and when the last of them went (it may be drawn over after that) */
    int users = 0;
    int last = 0;
    /** the how-manieth slot it is that was put into `slots` (the original's Map is gone through in that order) */
    uint64_t seq = 0;
  };

  /** How a sign is drawn: from a bundled graphic (mirrored or not), by signart, as a town sign, or as the simple fallback. */
  struct Plan {
    enum How { Svg, Art, Town, Fallback };
    How how = Fallback;
    /** ('': none) */
    std::string svg = {};
    bool mirror = false;
  };

  struct ManifestEntry {
    double width = 0;
    double height = 0;
    std::string shape = {};
  };

  std::string baseUrl_;
  std::unordered_map<std::string, std::string> svgs_;
  std::unordered_map<std::string, ManifestEntry> manifest_;
  Canvas canvas_;
  Ctx2D ctx_;
  MaterialPtr postMat_;
  GeometryPtr postGeo_;
  std::unordered_map<std::string, std::shared_ptr<Slot>> slots_;
  uint64_t slotSeq_ = 0;
  /** which cells of the atlas are taken (cell 0 is the plain grey back side) */
  std::array<uint8_t, COLS * ROWS> taken_{};
  /** counts the groups that went, to know which unused sign went longest ago */
  int gone_ = 0;
  /** the codes each group of posts shows (released when its tile goes) */
  std::unordered_map<const Object3D*, std::vector<std::string>> groupCodes_;
  std::unordered_map<std::string, std::shared_ptr<Group>> perTile_;
  bool dirty_ = false;
  /** (what `this` is still alive for, when a graphic is decoded late) */
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

  /**
   * How a sign is drawn: its own graphic where there is one and the plate
   * shows nothing the map adds (a time, a value) – mirrored, or of another
   * number, where that is the same sign –, else by signart, else the
   * graphic of the sign it is a variant of (101-12 → 101), else the simple
   * fallback.
   */
  Plan plan(const std::string& code) const;
  double aspectOf(const std::string& code) const;
  /** A free cell (or two side by side for a wide plate) – drawing over signs no group shows any more, those that went longest ago first; -1 if there is none. */
  int freeCell(bool wide);
  Slot& slot(const std::string& code);
  void draw(const std::string& code, double x, double y, double w, double h);
  /** The graphic of a sign into its place in the atlas once the host has decoded it (`img.onload`, `img.onerror` of the original). */
  Task<void> rasterise(std::string code, std::string base, std::string text, bool mirror, double x, double y, double w, double h, std::shared_ptr<Slot> mine);
  /** Simple procedural rendering of the most important signs. */
  void fallback(const std::string& code, double x, double y, double w, double h);
  /** `missing.add(base)` */
  void addMissing(const std::string& base);
  /** `svgs[name]`, if there is such a graphic (null: none) */
  const std::string* svgOf(const std::string& name) const;
};

}  // namespace fs
