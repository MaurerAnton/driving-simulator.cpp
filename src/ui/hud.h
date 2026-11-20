// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Menus and the in-game HUD (plain DOM in the original; here a widget tree
 * the platform renders).
 *
 * Origin: src/ui/hud.ts (MaurerAnton/driving-simulator @ 5b6ddc7, v0.1.0).
 * Every panel, row, card and toast of the original is represented; the
 * platform (HudHost) turns the tree into elements and forwards clicks.
 */
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "game/address.h"
#include "game/i18n.h"
#include "game/nav.h"
#include "game/settings.h"
#include "input/input.h"
#include "platform/canvas2d.h"
#include "sim/rules.h"
#include "ui/notices.h"
#include "world/world.h"

namespace fs {

struct AddressPicker;   // defined in ui/addresspicker.h (kept out: it needs HudNode)

/** What the HUD shows this frame. */
struct HudState {
  double speedKmh = 0;
  double rpm = 0;
  std::string gear = "N";
  /** km/h; 0 = no limit; -1 = unknown/off-road */
  int limit = -1;
  int zone = 0;
  struct Junction {
    bool set = false;
    double dist = 0;
    int role = 0;
    int ctrl = 0;
    int signal = -1;
  } junction;
  std::string navText;
  std::string navSub;
  int navArrow = 0;
  /** a turn is announced (so a notice must not hide it) */
  bool navActive = false;
  std::string tip;
  /** what tapping the tip does on a touch screen: a key code ("" = nothing) */
  std::string tipAction;
  bool showDash = false;
  int indicator = 0;
  bool hazard = false;
  bool blinkOn = false;
  bool handbrake = false;
  bool seatbelt = false;
  bool engineOn = false;
  bool lights = false;
  int points = 0;
  int faults = 0;
  double x = 0, z = 0, yaw = 0;
};

struct StartRequest {
  std::string mode;
  int spawn = 0;
  /** start at this address instead of one of the car parks (0.5.0) */
  AddressHit address;
  bool hasAddress = false;
};

struct ResultSummary {
  std::string title;
  bool exam = false;
  bool passed = false;
  std::vector<Violation> log;
  int points = 0;
  double distanceKm = 0;
  double minutes = 0;
};

/** One widget of the HUD tree. */
struct HudNode {
  std::string tag;    // div, h1, p, button, span, canvas, …
  std::string cls;
  std::string text;
  /** innerHTML-style markup the original built with template strings */
  std::string html;
  std::vector<std::unique_ptr<HudNode>> children;
  std::function<void()> click;
  /** widget identity so the platform can find and update it */
  std::string id;

  HudNode* add(std::unique_ptr<HudNode> child) {
    children.push_back(std::move(child));
    return children.back().get();
  }
};

/** What the platform must implement for the HUD to work. */
class HudHost {
 public:
  virtual ~HudHost() = default;
  /** Replace the visible tree with `root` (or clear it when null). */
  virtual void setRoot(HudNode* root) = 0;
  /** Update one widget's text (found by id). */
  virtual void setText(const std::string& id, const std::string& text) = 0;
  /** Update one widget's class list. */
  virtual void setClass(const std::string& id, const std::string& cls) = 0;
  /** Show or hide one widget. */
  virtual void setVisible(const std::string& id, bool visible) = 0;
  /** Paint one widget's canvas (the mini map). */
  virtual void paint(const std::string& id, const Canvas2D& canvas) = 0;
  /** A transient message (a toast) was added. */
  virtual void toastAdded() {}
};

class Hud {
 public:
  Hud(HudHost& host, Settings& settings, World& world);
  ~Hud();   // out of line: AddressPicker is an incomplete type in this header

  std::function<void(const StartRequest&)> onStart;
  std::function<void()> onResume;
  std::function<void()> onQuit;
  std::function<void()> onSettingsChanged;
  std::function<void()> onReality;
  /** the street-name block was tapped: pause menu (there is no Esc key on a phone) */
  std::function<void()> onPause;
  /** free drive: jump to an address (from the pause menu) */
  std::function<void(const AddressHit&)> onTeleport;
  /** fetches the address list when somebody first types an address */
  std::function<AddressIndex*()> loadAddresses;
  /** free drive: the pause menu offers to jump to an address */
  bool canTeleport = false;
  std::vector<Lane*> routeLanes;

  LangSetting langSetting() const { return settings_.lang; }
  std::string t(const std::string& key) const;
  std::string t(const std::string& key,
                const std::vector<std::pair<std::string, std::string>>& p) const;

  bool menuOpen() const { return overlay_ != nullptr; }

  void showLoading(const std::string& text);
  void showError(const std::string& text);
  void showMenu(const std::vector<SpawnJson>& spawns, int lastSpawn = 0);
  void showSettings(const std::function<void()>& back);
  void showControls(const std::function<void()>& back);
  void showAbout(const std::function<void()>& back);
  void showIntro(const std::string& title, const std::string& text, const std::function<void()>& go);
  void showPause();
  /** Free drive: put the car in front of an address (0.5.0). */
  void showTeleport();
  void showResult(const ResultSummary& r);
  void hideOverlay();

  void setHudVisible(bool v);
  /** A fault or hint of the rule monitor: shown in the block of the street name. */
  void toast(const Violation& v, double seconds = -1);
  void message(const std::string& title, const std::string& sub = "");
  void clearNotices() { notices_.clear(); }
  void update(const HudState& s, double now);

  /** The mini map, drawn into a canvas the platform paints. */
  void drawMap(const HudState& s);

  /**
   * Steering bar, pedals and buttons for driving with the fingers.  They are
   * always built and shown only in touch mode (see setInputMode).
   */
  void attachInput(Input& input);
  /** Fingers or keys?  Shows the on-screen controls or the key reminder. */
  void setInputMode(const std::string& mode);

 private:
  HudHost& host_;
  Settings& settings_;
  World& world_;
  std::unique_ptr<HudNode> overlay_;
  std::unique_ptr<HudNode> hud_;
  double lastDom_ = 0;
  double lastMap_ = 0;
  std::string lastPrio_;
  Canvas2D mapCanvas_{220, 220};
  /**
   * Notices (faults, hints) take the place of the street name; if several are
   * current, the gravest one shows.
   */
  NoticeBoard notices_;
  const Notice* noticeShown_ = nullptr;
  std::string mode_ = "keys";
  Input* input_ = nullptr;
  /** the "⋯" sheet of the on-screen controls is open */
  bool moreVisible_ = false;
  std::string tipAction_;
  HudState lastState_;
  bool hasLastState_ = false;
  std::string keysFor_;
  /** free drive: the start menu's address picker (kept alive while open) */
  std::unique_ptr<AddressPicker> menuPicker_;
  /** the address the picker chose (carried into the StartRequest) */
  AddressHit pickedAddress_;
  bool hasPickedAddress_ = false;

  void setOverlay(std::unique_ptr<HudNode> node);
  std::unique_ptr<HudNode> buildHud();
  /** The short reminder of the keys, depending on the gearbox chosen. */
  std::string keyReminder() const;
};

}  // namespace fs
