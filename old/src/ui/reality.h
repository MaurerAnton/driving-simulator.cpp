// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The reality view: real photographs of where the car is, from the player's
 * own files and from the free street-level imagery services.
 *
 * The panel is a widget tree (like the HUD) the platform renders; the
 * searching, the ranking and the state are the original's.
 *
 * Origin: src/ui/reality.ts (MaurerAnton/driving-simulator @ 578e397, v0.4.0).
 */
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "game/settings.h"
#include "render/panorama.h"
#include "reality/local.h"
#include "reality/providers.h"
#include "ui/hud.h"

namespace fs {

struct RealityPlace {
  double lat = 0, lon = 0;
  /** compass direction the car faces, degrees */
  double heading = 0;
  /** street name, may be empty */
  std::string name;
};

/** What the panel needs from the game and the platform. */
class RealityHost {
 public:
  virtual ~RealityHost() = default;
  virtual Settings& settings() = 0;
  virtual void saveSettings() = 0;
  virtual void close() = 0;
  /** can this position be meant (is it on the map)? */
  virtual bool plausible(double lat, double lon) const = 0;
  /** widest texture the graphics chip takes */
  virtual int maxTexture() const = 0;
  /** the map with aerial photographs, close up on where the car is */
  virtual void openAerial() = 0;
  /** pictures of other sources near a place (the players' photos on a server) */
  virtual std::vector<StreetImage> extraImages(double lat, double lon) {
    (void)lat;
    (void)lon;
    return {};
  }
};

/** search radius in metres: first close by, then further out */
constexpr double NEAR_RADIUS = 90;
constexpr double FAR_RADIUS = 300;

class RealityPanel {
 public:
  RealityPanel(HudHost& host, RealityHost& realityHost, HttpBackend* http = nullptr);

  void open(const RealityPlace& place);
  void close();

  /**
   * Draw the panorama, if one is showing.  (While the panel is open the world
   * is hidden behind it and need not be drawn at all.)
   */
  void render(WebGLRenderer& renderer, double left, double top, double width, double height);

  /** The panel's widget tree (the platform renders it). */
  HudNode* root() { return root_.get(); }
  bool isOpen() const { return root_ != nullptr; }

  /** The picture currently shown, and the ranked candidates. */
  const StreetImage* current() const { return current_; }
  const std::vector<StreetImage>& all() const;

  /** Ask one enabled service for pictures (the platform performs the HTTP). */
  void search(ImageSource source);
  /** Take over the player's own pictures (nothing is uploaded). */
  void importFiles(const std::vector<std::pair<std::string, std::vector<uint8_t>>>& files);

  /**
   * Called while the panel is open: the copy button says the plain word again
   * a moment after it was pressed (the original's setTimeout).
   */
  void tick();

  /** For the automated check of the page: what is showing right now. */
  std::string debugInfo() const;

 private:
  HudHost& host_;
  RealityHost& game_;
  HttpBackend* http_;   // the platform's HTTP; null means "no network here"
  std::unique_ptr<HudNode> root_;
  RealityPlace place_;
  std::map<ImageSource, std::vector<StreetImage>> found_;
  std::map<ImageSource, std::string> status_;
  std::map<std::string, std::vector<StreetImage>> cache_;
  std::vector<StreetImage> own_;
  StreetImage* current_ = nullptr;
  bool picked_ = false;
  double radius_ = NEAR_RADIUS;
  std::string note_;
  double openedAt_ = 0;
  /** the copy button said what happened (and says the plain word again a moment later) */
  bool copied_ = false;
  double copiedAt_ = 0;
  HudNode* copyBtn_ = nullptr;
  int frames_ = 0;
  PanoramaView pano_;

  std::string t(const std::string& key) const;
  std::string t(const std::string& key,
                const std::vector<std::pair<std::string, std::string>>& p) const;
  void build();
  void refresh();
  void select(const StreetImage& image);
  void showEmpty();
  void setCaption(const StreetImage& image);
};

}  // namespace fs
