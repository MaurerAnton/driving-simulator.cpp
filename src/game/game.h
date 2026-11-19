// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Game orchestration: loading, menus, the main loop, cameras, lessons.
 *
 * Origin: src/game/game.ts (MaurerAnton/driving-simulator @ 5b6ddc7, v0.1.0).
 * The state machine (loading/menu/intro/placing/drive/pause/result), the
 * physics stepping, the lesson bookkeeping and the camera modes are all
 * here; the platform supplies the frame clock, the query parameters and the
 * DOM (through HudHost).
 */
#pragma once

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "audio/audio.h"
#include "engine/math.h"
#include "game/app.h"
#include "game/i18n.h"
#include "game/nav.h"
#include "game/settings.h"
#include "input/input.h"
#include "render/actors.h"
#include "render/car.h"
#include "render/props.h"
#include "render/signs.h"
#include "render/tiles.h"
#include "sim/collision.h"
#include "sim/ground.h"
#include "sim/rules.h"
#include "sim/signals.h"
#include "sim/traffic.h"
#include "sim/vehicle/vehicle.h"
#include "ui/hud.h"
#include "world/lanes.h"
#include "world/world.h"

namespace fs {

enum class GameState { Loading, Menu, Intro, Placing, Drive, Pause, Result };
enum class CameraMode { Cockpit, Chase, Free, Dev };

/** Everything the platform hands the game at boot. */
struct QueryParams {
  std::map<std::string, std::string> values;

  std::string get(const std::string& key) const {
    auto it = values.find(key);
    return it == values.end() ? "" : it->second;
  }
};

class Game {
 public:
  Game(HudHost& host, Platform& platform, AudioBackend& audioBackend, SignSource& signSource,
       TileSource& tileSource, int canvasWidth, int canvasHeight);

  Settings settings;
  App app;
  std::unique_ptr<LaneGraph> lanes;
  std::unique_ptr<SignalSystem> signals;
  std::unique_ptr<Traffic> traffic;
  std::unique_ptr<RuleMonitor> rules;
  std::unique_ptr<Navigator> nav;
  std::unique_ptr<Hud> hud;
  std::unique_ptr<Input> input;
  std::unique_ptr<Vehicle> vehicle;
  std::unique_ptr<WorldGround> ground;
  PropRenderer props;
  std::unique_ptr<SignRenderer> signs;
  PlayerCar car;
  TrafficRenderer actors;
  std::unique_ptr<SignalRenderer> signalHeads;
  SceneryCollider collider;
  GameAudio audio;
  GameState state = GameState::Loading;
  CameraMode cameraMode = CameraMode::Cockpit;

  /** Boot: build the world, wire the HUD, then go to the menu (or ?start=). */
  void boot(const QueryParams& q);
  /** Advance one frame: `now` is the platform's clock in seconds. */
  void frame(double now);

  std::string debugInfo() const;

  // exposed for the tests (the original keeps them private)
  void applySettingsCall() { applySettings(); }
  void onActionCall(Action a) { onAction(a); }
  void finishCall() { finish(); }
  void beginCall(const std::string& mode, int spawnIdx, bool skipIntro) {
    begin(mode, spawnIdx, skipIntro);
  }
  WorldJson& worldJsonRef() { return worldJson_; }

 private:
  HudHost& host_;
  Platform& platform_;
  SignSource& signSource_;
  TileSource& tileSource_;
 public:
  WorldJson worldJson_;   // handed in by the platform before boot()
 private:
  std::unique_ptr<World> world_;
  double last_ = 0, time_ = 0, acc_ = 0;
  int frames_ = 0;
  double mirrorTurn_ = 0;
  SpawnJson spawn_;
  bool hasSpawn_ = false;
  LessonDef lesson_;
  bool hasLesson_ = false;
  int targetIdx_ = 0;
  Edge* homeEdge_ = nullptr;
  std::set<Edge*> visited_;
  int basicsStep_ = 0;
  double startOdo_ = 0, startTime_ = 0;
  double grindT_ = 0, failT_ = -1;
  struct GoodSpot {
    double x = 0, y = 0, z = 0, yaw = 0, t = 0;
  } lastGood_;
  std::string lastNavSpoken_;
  double impact_ = 0;
  SurfaceHit hit_;
  double mpos_[3] = {0, 0, 0};
  double wheelY_[4] = {0.316, 0.316, 0.316, 0.316};
  struct FreeCam {
    Vector3 pos{0, 3, 8};
    double yaw = 0, pitch = -0.2;
  } free_;
  bool freeRelative_ = true;
  std::vector<std::string> keys_;
  DriverInput driver_;
  double autoDrive_ = 0;
  std::string tipText_;
  double tipUntil_ = 0;
  Vector3 tmpV_, eyeWorld_;
  Quaternion tmpQ_;
  CarVisualState visual_;
  bool started_ = false;

  void onTile(LoadedTile& tile, bool loaded);
  void applySettings();
  void toMenu();
  void begin(const std::string& mode, int spawnIdx, bool skipIntro = false);
  void tryPlace();
  void resume();
  void pause();
  void finish();
  void onAction(Action a);
  void resetToRoad();
  void stepDrive(double dt);
  void rememberGoodSpot(const PlayerState& p);
  Lane* currentLane();
  double laneFront();
  void updateLesson(double dt, const PlayerState& p);
  void updateVisuals(double dt);
  void speak(const std::string& what, double dist, const std::string& text);
  void updateCamera(double dt);
  std::string t(const std::string& key) const;
};

}  // namespace fs
