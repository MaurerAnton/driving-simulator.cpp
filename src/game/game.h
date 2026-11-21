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
#include "game/address.h"
#include "game/app.h"
#include "i18n/index.h"
#include "game/nav.h"
#include "game/faultlog.h"
#include "game/track.h"
#include "game/settings.h"
#include "input/input.h"
#include "render/actors.h"
#include "render/car.h"
#include "render/props.h"
#include "render/signs.h"
#include "render/tiles.h"
#include "render/panorama.h"
#include "sim/collision.h"
#include "sim/ground.h"
#include "sim/rules.h"
#include "sim/signals.h"
#include "sim/traffic.h"
#include "sim/vehicle/vehicle.h"
#include "ui/hud.h"
#include "ui/reality.h"
#include "world/lanes.h"
#include "world/world.h"

namespace fs {

struct RealityHostImpl;   // defined in game.cpp

enum class GameState { Loading, Menu, Intro, Placing, Drive, Pause, Reality, Result };
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
  ~Game();   // out of line: realityHost_ is an incomplete type in this header

  Settings settings;
  App app;
  std::unique_ptr<LaneGraph> lanes;
  std::unique_ptr<SignalSystem> signals;
  std::unique_ptr<Traffic> traffic;
  std::unique_ptr<RuleMonitor> rules;
  std::unique_ptr<Navigator> nav;
  std::unique_ptr<Hud> hud;
  std::unique_ptr<FaultLog> faultLog_;
  std::unique_ptr<DrivenPath> path_;
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
  void beginCall(const std::string& mode, int spawnIdx, bool skipIntro,
                 const AddressHit* address = nullptr) {
    begin(mode, spawnIdx, skipIntro, address);
  }
  WorldJson& worldJsonRef() { return worldJson_; }

 private:
  friend struct RealityHostImpl;   // the reality panel's bridge (game.cpp)
  /** where the log and the path are kept (null: only for this session) */
  KeyValueStore* store_ = nullptr;
  /** the faults of the drive that just ended join the log (0.7.0) */
  void rememberDrive(const ResultSummary& r);
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
  /** the line the lesson shows under the street name ("2 · Vorfahrt … · 1/3") */
  std::string lessonLine_;
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
  std::unique_ptr<RealityHostImpl> realityHost_;  // defined in game.cpp
  std::unique_ptr<RealityPanel> reality_;
  /** the address list, fetched once somebody first searches for an address */
  std::unique_ptr<AddressIndex> addressIndex_;
  bool addressAsked_ = false;
  /** lanes of the connected network (for finding a spot by address) */
  std::unique_ptr<std::set<int>> mainLanes_;
  /** fingers or keys: what the driver last used (the HUD follows it) */
  std::string inputMode_ = "keys";
  /** ?notice=red|yellow|both: show sample notices for the automated check */
  std::string autoNotice_;
  std::string autoReality_;
  int realityFrames_ = 0;
  double impact_ = 0;
  double rolledT_ = 0;
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
  void begin(const std::string& mode, int spawnIdx, bool skipIntro = false,
             const AddressHit* address = nullptr);
  /** The address list (house numbers of the map), read once somebody searches. */
  AddressIndex* loadAddresses();
  /** The spot on the road in front of an address, as a starting point. */
  bool spawnAt(const AddressHit& hit, SpawnJson& out);
  /** Free drive: jump to an address (from the pause menu). */
  void teleportTo(const AddressHit& hit);
  void openReality();
  /** the full-screen map of the town (0.7.0; drawn when ui/bigmap is ported) */
  void openBigMap();
  void closeReality();
  void tryPlace();
  void resume();
  void pause();
  void finish();

  /** the log of faults of this and of earlier drives (0.7.0) */
  FaultLog& faultLog() { return *faultLog_; }
  /** the driven path of this session, for the map (0.7.0) */
  DrivenPath& drivenPath() { return *path_; }
  /**
   * What the browser's localStorage is here: the store the fault log and the
   * driven path keep.  The platform hands its storage in (or none).
   */
  void setStore(KeyValueStore* store) { store_ = store; }
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
