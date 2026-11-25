// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Game orchestration: loading, menus, the main loop, cameras, lessons.
 *
 * Origin: src/game/game.ts @ v1.7.7
 *
 * The class is one, its methods lie in several files:
 *   game.cpp        the constructor, boot, the settings, the menu
 *   game_drive.cpp  begin … stepDrive, the spots
 *   game_frame.cpp  frame, updateVisuals, updateCamera, the lesson
 *   game_map.cpp    the map: addresses, teleporting, the ways of the whole map      (hub-b)
 *   game_trips.cpp  drives to places, tours, the autopilot, the flying camera       (hub-b)
 *
 * What 2.0.0 leaves for later (docs/STATUS.md) has no field here: where the original's
 * code would use it stands `// (later: <name> – game.ts:<line>)`.
 */
#pragma once

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "core/geom.h"
#include "core/task.h"
#include "engine/engine.h"
#include "game/app.h"
#include "game/ce.h"
#include "game/lessonplaces.h"
#include "game/openplay.h"
#include "game/settings.h"
#include "game/vehicles.h"
#include "platform/dom.h"
#include "render/bike.h"
#include "render/car.h"
#include "render/fleet.h"
#include "render/navscreen.h"
#include "render/tiles.h"
#include "sim/playerstate.h"
#include "sim/vehicle/types.h"
#include "world/lanes.h"
#include "world/regions.h"
#include "world/routegraph.h"
#include "world/tilefmt.h"
#include "world/types.h"
#include "world/world.h"

namespace fs {

// ---- the layers below, by the original's names (each .cpp includes what it uses) ----
class AddressIndex;
/** (game/address.h) */
using AddressHit = StartAddress;
class ArcadeDrive;
class AutoDriver;
class CraneModel;
class CraneWork;
/** (sim/crane.h: 'tower' | 'mobile' | 'loader') */
using CraneType = std::string;
class DrivenPath;
class FarTerrain;
class FaultLog;
class FlyCam;
class GrassRenderer;
class GameAudio;
struct LessonDef;
struct MapFollowers;
class Navigator;
class Outdoors;
class NetIndex;
class ParkedCarRenderer;
class PedLightRenderer;
struct PedPole;
class PropRenderer;
class RouteWatch;
class RuleMonitor;
class SceneryCollider;
class SignalRenderer;
class WorldLayer;
class SignalSystem;
class SignRenderer;
class Traffic;
class TerrainSource;
class TrafficRenderer;
class PlayerTruck;
class YardScene;
class YardTask;
class Trailer;
class Vehicle;
class WindscreenView;
class WorldGround;
// (src/ui and src/input)
class BigMap;
class Hud;
class Input;
class ScreenTap;
class TiltSteer;
class TouchStick;
class WorldTap;
struct ResultSummary;
struct RoadSource;
struct TourPreview;
// (the aeroplane: src/sim/flight.h, autopilot.h, airfield.h; src/render/plane.h, airfield.h; ui/hud.h)
class Aircraft;
class AirfieldGround;
class AirfieldRenderer;
struct AirJson;
class FlightAssist;
class FlightAutopilot;
struct FlightControls;
class Ground;
class PlaneModel;
struct PlaneVisualState;
struct RunwayChoice;
struct TramStopChoice;
struct AutopilotState;
/** What the aeroplane's autopilot is doing, in a few words a pilot would use (a text key) – game_plane.cpp. */
std::string apPhaseKey(const AutopilotState& st);

/** 'loading' | 'menu' | 'intro' | 'placing' | 'drive' | 'pause' | 'reality' | 'map' | 'result' | 'replay' | 'theory' */
using State = std::string;
/** the driver's seat, behind the car, a camera that is moved about freely – and a fixed one for pictures (?cam=): 'cockpit' | 'chase' | 'free' | 'dev' */
using CameraMode = std::string;

inline constexpr double PHYS_DT = 1.0 / 240;

// (Which model a vehicle is drawn with: render/fleet.h VehicleModel is what all of them have in common.)

/** The motorcycle of each class has its own build (render/bike.ts): moped, 125, 35 kW, the big one. */
BikeVariant bikeVariant(const std::string& kind);

/** Which model a vehicle is drawn with: its kind of model – for a motorcycle, the build of its class. */
std::string modelKey(const VehicleDef& def);

/** `new URLSearchParams(location.search)`: what the address of the page asks for. */
class URLSearchParams {
 public:
  URLSearchParams() = default;
  /** "?start=free&lang=de" (with or without the question mark) */
  explicit URLSearchParams(const std::string& search);
  /** the first value of a name – null: not given */
  std::optional<std::string> get(const std::string& name) const;
  bool has(const std::string& name) const;

 private:
  std::vector<std::pair<std::string, std::string>> list_;
};

class Game {
 public:
  /** a place and a direction on the ground */
  struct Spot {
    double x = 0;
    double y = 0;
    double z = 0;
    double yaw = 0;
  };
  struct Place {
    double x = 0;
    double z = 0;
    double yaw = 0;
  };

  App app;
  const dom::El ui;
  Settings settings = loadSettings();
  std::unique_ptr<LaneGraph> lanes;
  std::unique_ptr<SignalSystem> signals;
  std::unique_ptr<Traffic> traffic;
  std::unique_ptr<RuleMonitor> rules;
  std::unique_ptr<Navigator> nav;
  std::unique_ptr<Hud> hud;
  std::unique_ptr<Input> input;
  std::unique_ptr<Vehicle> vehicle;
  /** the tiles as the ground sees them (sim/ground.h: TerrainSource) */
  std::unique_ptr<TerrainSource> terrain;
  std::unique_ptr<WorldGround> ground;
  /** who follows the road network as it comes in pieces (sim/follow.h) */
  std::unique_ptr<MapFollowers> followers;
  const std::unique_ptr<PropRenderer> props;
  // (grass: below, '--- weather, seasons, the windscreen ---' – game.ts:290)
  std::unique_ptr<SignRenderer> signs;
  PlayerCar car;
  const std::unique_ptr<TrafficRenderer> actors;
  /** the truck of the class-CE lessons with its trailer (built when it is first driven; owned by `vehicleModels`) */
  PlayerTruck* truck = nullptr;
  /** the trailer behind the vehicle (null: none) */
  std::unique_ptr<Trailer> trailer;
  /** the vehicle driven now (by its licence class), and the models of the vehicles made so far */
  const VehicleDef* def = &VEHICLES.at("car");
  /**
   * the models of the vehicles driven so far, by modelKey (each motorcycle class has its own build) – the car's
   * from the start (without it, the first drive of class B made a motorcycle for the car: the model of a kind
   * not yet in here is made when it is first driven)
   */
  std::map<std::string, VehicleModel*> models{{"car", &car}};
  // (later: crane, forklift, carTrailer – game.ts:306)
  /** the car's centre display, for the HUD to draw the map on */
  std::unique_ptr<VehicleScreen> carScreen;
  /** what the vehicle's lamps showed in the last picture (for the trailer's) */
  const CarVisualState* lastVisual = nullptr;
  /** the truck with the trailer of the class CE (its yard tasks, the coupling) */
  bool isTruck() const { return def->model == "truck" || def->model == "semi"; }
  // (later: yardScene, task, tasksLeft, taskCount, taskFailed, taskSub, drill, drillScene, drillPos, laidTry – game.ts:320)
  /** where the drive in traffic begins once the tasks on the yard are done */
  std::optional<SpawnJson> roadSpawn;
  /**
   * A drive to places the player chose, or along a tour that was planned.
   * `active`: a demonstration drive – the car drives by itself, by the rules,
   * to watch how the way is driven; otherwise the player drives and is shown
   * the way.
   */
  struct Demo {
    std::vector<SpawnJson> stops;
    int index = 0;
    std::unique_ptr<AutoDriver> driver;
    bool active = false;
    double look = 0;
    std::string remark;
    double takeT = 0;
    /** the player has taken the wheel for a moment (the assisted autopilot): it drives on when let go, after `freeT` s without a hand on it */
    bool held = false;
    double freeT = 0;
    double lost = 0;
    /** when a way may be looked for again (after a search that found none) */
    double searchAt = 0;
    double stuck = 0;
    /** how often it was put on (the last minute), and when last */
    int rescues = 0;
    double rescuedAt = 0;
    /** a tour: the ways to drive (world/routegraph.ts), the way they begin behind, and what it comes to */
    struct Tour {
      std::vector<int> ways;
      int from = 0;
      double metres = 0;
      double fresh = 0;
      double odo = 0;
      bool set = false;
      std::vector<float> line;
    };
    std::optional<Tour> tour;
    Demo();
    ~Demo();
    Demo(Demo&&) noexcept;
    Demo& operator=(Demo&&) noexcept;
  };
  std::unique_ptr<Demo> demo;
  /** the road network as the route planner sees it, of a map in one piece (made when a way is first planned) */
  std::optional<RouteGraph> routeGraph;
  /**
   * The roads of the whole map – of a map whose road network comes in pieces
   * around the car: for routes and tours beyond what is loaded, for the big
   * map, for places far away.  Read in the background once the game runs.
   */
  std::unique_ptr<NetIndex> netIndex;
  bool signalsDirty = false;
  double signalsBuilt = 0;
  /** the street of the address the car is being put at (to find the right road once the roads there are loaded) */
  std::string spawnStreet;
  double tourSeed = 1;
  /** open the map for planning such a drive as soon as the car stands (start "demo" of the menu) */
  bool planDemo = false;
  /** a set route being driven (game/routepresets.ts): is it kept?  And one to start once the drive is placed (from the menu). */
  std::unique_ptr<RouteWatch> routeWatch;
  std::string presetPending;
  /** a lesson in a real place (game/lessonplaces.ts): what to look for once the roads round the start are there – and the destination found with it */
  std::optional<LessonStart> placeSearch;
  Edge* placeGoal = nullptr;
  // (later: lessonWatch – game.ts:383)
  /** the lesson's start panel was answered (ui/lessonstart.ts) */
  bool lessonChosen = false;
  bool camSnap = false;
  double demoHead = 0;
  // (later: rail, railActors, railOthers: trams and trains on the tracks around the player – game.ts:388)
  /** what stands in the way of the traffic beside the player's vehicle (its trailer; later: the trams' cars) */
  std::vector<PlayerState> railBoxes;
  std::unique_ptr<SignalRenderer> signalHeads;
  // (later: signalTimers, queueLabels, extraSigns – game.ts:394)
  /** the pedestrian lights: their poles (with the buttons) and their heads */
  std::vector<PedPole> pedPoles;
  const std::unique_ptr<PedLightRenderer> pedHeads;
  // (later: passengerView – game.ts:403)
  /** the ground beyond the map's tiles: the hills on the horizon */
  const std::unique_ptr<FarTerrain> far;
  // (later: wading, transit, roadEvents, roadworks, levelCrossings, crossingView – game.ts:407)
  // (later: the aeroplane: aircraft … flightControls – game.ts:416)
  // (later: lines, lineStopsDirty, linesFor – game.ts:437)
  const std::unique_ptr<SceneryCollider> collider;
  // (outdoors, shelterOf: below, '--- weather, seasons, the windscreen ---'; later: explosion, wrecked – game.ts:443)
  const std::unique_ptr<GameAudio> audio;
  // (later: sirens, services – game.ts:452)
  State state = "loading";
  CameraMode cameraMode = "cockpit";

  double last = 0;
  double time = 0;
  double acc = 0;
  int frames = 0;
  int fpsFrames = 0;
  double fpsSince = 0;
  int mirrorTurn = 0;
  // (windscreen: below, '--- weather, seasons, the windscreen ---'; later: mirrorZoom, hudMirror – game.ts:466)
  /** a tap on the vehicle's screen: the big map or the corner map (ui/screentap.ts) */
  std::unique_ptr<ScreenTap> screenTap;
  std::optional<SpawnJson> spawn;
  const LessonDef* lesson = nullptr;
  /**
   * free drive: the car was spawned in gear (D1) with the handbrake off, held
   * still until the driver gives gas or the brake (settings: none – the spawn
   * is ready to go but does not creep off on its own).  Off for lessons/exams,
   * which start the full programme (handbrake on, released by the driver).
   */
  bool spawnHold = false;
  /** steering by tilting the phone (settings: tiltSteer, ui/tiltsteer.ts) – null while off */
  std::unique_ptr<TiltSteer> tilt;
  int targetIdx = 0;
  /** the road a lesson began on (its number in the map: the road itself may be dropped and loaded again on the way), or -1 */
  int homeEdge = -1;
  std::unordered_set<Edge*> visited;
  int basicsStep = 0;
  double startOdo = 0;
  double startTime = 0;
  double grindT = 0;
  double failT = -1;
  struct GoodSpot : Spot {
    double t = 0;
  };
  GoodSpot lastGood;
  std::string lastNavSpoken;
  double impact = 0;
  double rolledT = 0;
  SurfaceHit hit = newSurfaceHit();
  std::array<double, 3> mpos{0, 0, 0};
  std::array<double, 4> wheelY{0.316, 0.316, 0.316, 0.316};
  struct Free {
    Vector3 pos = Vector3(0, 3, 8);
    double yaw = 0;
    double pitch = -0.2;
  };
  Free free;
  bool freeRelative = true;
  /**
   * The free camera: where it is (from the car, in the directions of the map –
   * it travels along with the car, but does not turn with it) and where it looks.
   */
  struct Fc {
    Vector3 off;
    double yaw = 0;
    double pitch = -0.3;
    bool ready = false;
  };
  Fc fc;
  std::unordered_set<std::string> keys;
  DriverInput driver;
  /** arcade driving (a setting): D and R by the pedals, held at a standstill, the engine always running (game/arcadedrive.ts) */
  const std::unique_ptr<ArcadeDrive> arcadeDrive;
  double autoDrive = 0;
  std::string tipText;
  // (later: beams, the headlights – game/beamplay.ts with sim/beams.ts, game.ts:514)
  // (later: wildView – game.ts:516)
  /** every fault of this and of earlier sessions */
  const std::unique_ptr<FaultLog> faultLog;
  // (later: online – game.ts:520)
  /** where the car has been, for the map */
  const std::unique_ptr<DrivenPath> path;
  std::unique_ptr<BigMap> bigMap;
  /** 2.0: the world behind the big map – every region (made when the map is first opened) */
  std::shared_ptr<WorldLayer> worldLayer;
  /** the roads the big map draws ways on (ui/pathdraw.h roadSource: made anew each time it asks) */
  std::unique_ptr<RoadSource> mapRoads;
  // (later: transitSrc, watcher, tramLineRef – game.ts:531)
  /** what is to be done once the vehicle is placed */
  std::function<void()> afterPlace;
  // (later: airAgl – game.ts:535)
  /** what the map was opened from – closing it leads back there (from the main menu: a place tapped on it is where the drive will begin): 'drive' | 'pause' | 'menu' */
  std::string mapFrom = "drive";
  /** the car is being put somewhere else in the middle of a drive: distance and time go on */
  bool keepClock = false;
  bool hbWarned = false;
  std::string autoMap;
  // (later: autoTram – game.ts:542)
  std::vector<std::vector<double>> autoDemo;
  double autoTour = 0;
  bool autoSelf = false;
  int mapFrames = 0;
  Vector3 tmpV;
  Vector3 camTarget;
  Quaternion tmpQ;
  Matrix4 tmpM;
  Vector3 headV;
  // (later: figure, recorder, replay, replayFrom, replayLive, replayShown, replayHidden, replayCam – game.ts:553)
  std::unique_ptr<ResultSummary> lastResult;
  Vector3 eyeWorld;

  Game(const dom::El& canvas, const dom::El& ui);
  ~Game();
  Game(const Game&) = delete;
  Game& operator=(const Game&) = delete;

  Task<void> boot(URLSearchParams q);
  /**
   * the loop runs: `frame` asks for the next picture by itself (plat::requestAnimationFrame) –
   * a test clears it and calls `frame(now)` as it likes
   */
  bool looping = false;

  /** ?look=yaw,pitch: where the driver looks, for pictures */
  std::optional<std::array<double, 2>> debugLook;

  double devFov = 60;
  /**
   * For tools/dev/shot.mjs: a camera that stands still – `h` metres above the
   * ground at (x, z), or at the height `h` itself (`absolute`), looking
   * `heading`° (clockwise from north) and `pitch`° up.  The ground's height, or NaN where there is none yet.
   */
  double devView(double x, double h, double z, double heading, double pitch = 0, double fov = 60, bool absolute = false);

  /** 'touch' | 'keys' */
  std::string inputMode = "keys";
  /** (not valid(): not asked for yet) */
  Task<AddressIndex*> addressIndex;
  /** (the index itself, kept for as long as the game runs) */
  std::unique_ptr<AddressIndex> addresses;
  std::optional<std::unordered_set<int>> mainSet;
  bool netAsked = false;
  /** 2.0: failed reads of the whole map's network so far, when the next may be, and whether the game gave up on it */
  int netTries = 0;
  double netRetryAt = 0;
  bool netGaveUp = false;

  // ---- game_map.cpp, game_trips.cpp (hub-b) ----
  void wantNet();
  std::optional<std::vector<int>> waysToMotorway(const RouteGraph& g, Lane& lane, double x, double z);
  /** (null: every lane) */
  std::function<bool(const Lane& l)> usableLane();
  RouteGraph* routeGraphNow();
  std::optional<SpawnJson> roadSpot(double x, double z, const std::string& street, double maxDist);
  int edgeNear(double x, double z);
  /** null: the map has no addresses (ui/hud.h and ui/bigmap.h ask for it as this) */
  Task<AddressIndex*> loadAddresses();
  std::optional<SpawnJson> spawnAt(const AddressHit& hit);
  void teleportTo(const AddressHit& hit);
  void teleportSpot(const SpawnJson& spot);
  void openMap(bool closeUp = false);
  Lane* laneAt(double x, double z, double yaw);
  Task<std::optional<TourPreview>> planTour(double metres, std::optional<SpawnJson> end, std::function<void(double share)> progress = nullptr);
  /** `tour`: null – a drive to the stops */
  void startTrip(const std::vector<SpawnJson>& stops, bool self, const TourPreview* tour);
  void startPreset(const std::string& id);
  /** 'arrived' | 'taken' | 'lost' | 'ended' */
  void endDemo(const std::string& why);
  void updateDemo(double dt, PlayerState& p);
  void closeMap();
  bool autopilotDriving();
  void takeOverCar(int id);
  bool flyFree();
  void flyCamera(Camera& cam, double dt);
  bool flyHold();
  bool canTeleport();
  bool canDrive();
  void teleportHere(double x, double y, double z, double yaw);
  void driveHere(double x, double z);
  void turnAround();
  bool canTurnAround();
  struct TourWishes {
    std::function<bool(int way)> avoid = nullptr;
    std::function<double(int edge)> like = nullptr;
  };
  TourWishes tourWishes(const RouteGraph& g);

  // ---- game.cpp ----
  // (later: autoReality, realityFrames, reality – game.ts:1173)
  std::string autoNotice;
  void onTile(LoadedTile& tile, bool loaded);
  void applyLanguage();
  void applySettings();
  Task<void> firstRegionByIp();
  /** the region to open instead of the site's own map (world/regions.h; the App fetches its files by itself) – null: the site's map */
  Task<std::optional<Region>> chosenRegion();
  Task<void> setTiltSteer(bool on);
  // (later: applyDevMap, checkForNewerParts, onTileVersion – game.ts:1373)
  void toMenu();
  Vector3 menuFocus;
  // (later: timetable – game.ts:1501)
  VehicleModel& ensureModel(const VehicleDef& def);
  // (later: menuPreview … previewPos, setMenuPreview, menuPreviewKey, clearPreview, placePreview, nameSign, levelTrailer, standModel – game.ts:1528)
  // (later: showGroup … showSpots, buildShowroom – game.ts:1644)
  void parkedVisual(Vehicle& v);
  // (later: menuShow, the vehicle chosen behind the menu of rows – game.ts:1852)
  VehicleModel* makeModel(const VehicleDef& def);
  /** the model of the vehicle driven now (the original's getter `model`) */
  VehicleModel& model();
  void showModel(bool on);
  // (later: truckSpot, nextTask, endTasks, placeAgain, taskDone, updateTask, drillBegin, drillPlace, updateDrill – game.ts:1946)

  // ---- game_drive.cpp ----
  struct PendingIntro {
    std::string title;
    std::string text;
  };
  std::optional<PendingIntro> pendingIntro;
  /** `address`, `stop`, `runway`: null – none */
  void begin(const std::string& mode, int spawnIdx, bool skipIntro = false, const AddressHit* address = nullptr, const TramStopChoice* stop = nullptr,
             const RunwayChoice* runway = nullptr);
  void tryPlace();
  void resume();
  std::array<double, 2> focus();
  // (later: footAfterJump – game.ts:3477)
  /** "What's new" is to be shown when the menu first comes up */
  bool showNewsOnce;
  // (later: walkPending – game.ts:3483)
  void pause();
  void clockJumped();
  /** the last moment the sun was placed for (-1: to be placed, -2: the game's fixed afternoon) */
  double sunPlacedAt = -1;
  void updateSun();
  // (later: headlights – game/beamplay.ts, game.ts:3538)
  void finish();
  /** (input/input.ts: Action) */
  void onAction(const std::string& a);
  void resetToRoad();
  double resetHop = 0;
  struct ResetAt {
    double x = 0;
    double z = 0;
    double t = -100;
  };
  ResetAt resetAt;
  PolyProjection proj;
  PolySample smp = newPolySample();
  void putOnRoad(const Spot& spot);
  std::optional<Spot> freeSpot(double x, double z, double yaw, double ahead);
  void toAutomatic();
  bool arcadeNow();
  void stepDrive(double dt);
  double tipUntil = 0;
  void rememberGoodSpot(const PlayerState& p);
  Lane* currentLane();
  // (later: walker, markers, fadeEl, walkStart, doorT, doorWay, doorFrom – game.ts:4596; opens: below, "bikes")
  /** when the driver last looked back over the shoulder */
  double lookedBackAt = -100;
  // (later: walkSince – game.ts:4610; parkedSteer: below, "bikes")
  void changeVehicle(const VehicleKind& kind);
  /** where the next start puts the car: a car of the traffic taken over (takeOverCar) – null: as usual */
  std::optional<Place> parkedAt;
  // (later: spawnCars, intoParked, makeSpawnCars, canSpawnCar, canEnterParked, enterParked – game.ts:4865)
  SpawnJson exactSpot(const std::string& name, double x, double z);
  /** the places `exactSpot` made (x, z): a start place is a value here, the mark is kept beside it (the original's WeakSet exactSpots) */
  std::vector<std::array<double, 2>> exactSpots;
  bool isExactSpot(const SpawnJson& sp) const;
  std::optional<SpawnJson> startSpot(const AddressHit& hit);
  Place fitExact(const Place& sp, const Outline& o);
  // (later: menuSpot – game.ts:4982)
  void startWhereIAm();
  // (later: newVehicle, a new vehicle after one went up in flames – game.ts:5017)
  void signsBesideRoad(std::vector<SignPost>& signs);
  double footGround(double x, double z, double yRef);
  // (later: walkCtl – game.ts:5162)
  const std::unique_ptr<FlyCam> fly;
  std::unique_ptr<TouchStick> touchStick;
  std::unique_ptr<WorldTap> worldTap;
  // (later: parts, regionParts, versioned, partIndex, labelTile, pendingParts – game.ts:5167)
  /** the free camera flies and the vehicle stands where it was left */
  bool flyParked = false;
  // (later: on foot, in the tram, the bus line, the aeroplane, the tram driven – game.ts:5418 … 6247)

  // ---- game_frame.cpp ----
  void footFlyFrame();
  void buildPedLights();
  /** the junction whose lights were last said to be out of order, and for how long the car has stood at a lit green arrow */
  Node* toldDark = nullptr;
  double atArrow = 0;
  void signalTips(double dt);
  double laneFront();
  void updateLesson(double dt, PlayerState& p);
  std::string lessonLine;
  /** the route's lanes the HUD was last given (`hud.routeLanes !== nav.routeLanes`), and its roads for the rules (`rules.allowed`) */
  std::shared_ptr<const std::vector<Lane*>> hudRouteLanes;
  std::unordered_set<const Edge*> allowedEdges;
  /** `window.__fs.ready`: a picture of the game stands (tools/dev/shot.mjs waits for it) */
  bool ready = false;
  void frame(double now);
  CarVisualState visual;
  // (updateOutdoors: below, '--- weather, seasons, the windscreen ---'; later: soundAt – game.ts:6842)
  void updateVisuals(double dt);
  void speak(const std::string& what, double dist, const std::string& text);
  // (later: footGlance – game.ts:7265)
  /** the chase camera's place relative to the point it circles (smoothed) */
  Vector3 chaseOff;
  void updateCamera(double dt);
  /** what tools/dev/shot.mjs and the tests ask of the game, as JSON */
  std::string debugInfo();
  /** 2.0: where a frame's time goes (milliseconds, summed over `frames`; debugInfo shows the averages) */
  struct FrameTimes {
    double sim = 0, visuals = 0, scene = 0, mirrors = 0, render = 0, total = 0;
    int frames = 0;
  } frameTimes;
  // --- bikes ---
  /** the motorcycles made so far (render/bike.ts PlayerBike: each class its own build, in `models` by modelKey) */
  std::vector<std::unique_ptr<PlayerBike>> bikes;
  /** the parts that open, getting in and out, the seats (game/openplay.ts) */
  OpenPlay opens;
  /** the steering as it was left when the driver got out (game.ts:4610) */
  double parkedSteer = 0;
  /** The parts that open, and getting into the car or out of it: the door opens, one moves, the door shuts (game/openplay.ts). */
  void stepOpen(double dt);

  // --- the cranes ---
  /** the practice yard's frame (game/ce.ts YardFrame; null: the map has none) – the cranes' construction site is laid on it */
  std::optional<YardFrame> yardFrame;
  /** the work with a crane on the construction site (game/craneplay.ts; null: no crane is worked) */
  std::unique_ptr<CraneWork> crane;
  /** the cranes' models made so far (`models` points at them) */
  std::vector<std::unique_ptr<CraneModel>> craneModels;
  /** The work with a crane (game/craneplay.ts), told what it needs of the game. */
  std::unique_ptr<CraneWork> makeCraneWork(const CraneType& type);
  /** The crane: from driving it to working it, and back (the door key, or the button). */
  void craneToggle();
  /**
   * A step of a crane at work: its controls, the crane and its hook, the
   * loads, the tasks (game/craneplay.ts) – its vehicle stands meanwhile –
   * and the traffic beyond the site.
   */
  void stepCrane(double dt);
  /** `isCraneModel(this.model)`: the model of the vehicle driven now as a crane's (render/crane.ts) – null: it is none */
  CraneModel* craneModel();
  // --- the aeroplane ---
  /** the aeroplane being flown (null: none), its model, and the airfields of the map with their firm ground */
  std::unique_ptr<Aircraft> aircraft;
  std::unique_ptr<PlaneModel> planeModel;
  std::shared_ptr<const AirJson> air;
  /** the aeroplane's autopilot (take-off, holding height and heading, to a place, the circuit and landing), and the help for flying by hand */
  std::unique_ptr<FlightAutopilot> flightAuto;
  std::unique_ptr<FlightAssist> flightAssist;
  /** how long the stick has been moved while the autopilot flies (taking over), and a demonstration flight to begin once the aeroplane stands */
  double flightTake = 0;
  /** where the autopilot is to fly once it has taken off (a place chosen on the map while standing on the runway) */
  struct FlightGoto {
    double x = 0, z = 0, alt = 0;
  };
  std::optional<FlightGoto> flightGoto;
  bool planeDemoPending = false;
  std::unique_ptr<AirfieldGround> airGround;
  std::unique_ptr<AirfieldRenderer> airfieldView;
  /** a flight is to begin once the runway is placed */
  bool planePending = false;
  /** the throttle lever (0 … 1), the flap lever (0 … 3: 0°, 10°, 20°, 30°), the trim (−1 … 1), when it crashed */
  struct FlightLevers {
    double throttle = 0;
    int flaps = 1;
    double trim = 0.15;
    double crashT = -1;
  } flight;
  std::unique_ptr<FlightControls> flightControls;
  /** free flight: how high above the ground the aeroplane is to be put (0: on the runway – flyFrom) */
  double airAgl = 0;
  /** whether a flight asked for before the airfields were read has waited for them (once is enough: a map may have none) */
  bool airAwaited = false;
  /** the airfields of the map, once they are read: their ground, how they are drawn, the ends of their runways in the menu */
  void airRead(std::shared_ptr<const AirJson> json);
  void flyFrom(const SpawnJson& spot, double agl);
  void startPlaneAir(const SpawnJson& sp, double agl);
  double groundHeight(double x, double z);
  std::optional<RunwayChoice> firstRunway();
  /** the aeroplane's model, made the first time it is flown (the original's planeAdapter, kept beside the road vehicles' models) */
  PlaneModel& planeAdapter();
  void startPlane(const SpawnJson& sp);
  void placePlaneProxy();
  void stepFlight(double dt);
  void toggleFlightAuto();
  void afterFlightStep(Aircraft& a, double dt);
  PlaneVisualState planeVisual(Aircraft& a);
  /** `this.airGround ?? this.ground` */
  Ground& flightGround();
  // --- vehicles ---
  /** the box trailer behind a car (class BE) – or the semi-trailer of the articulated lorry (render/semitruck.ts TowedModel) */
  std::shared_ptr<TowedModel> carTrailer;
  /** the models made for the vehicles other than the car (`models` points into them) */
  std::vector<std::unique_ptr<VehicleModel>> vehicleModels;
  // class CE: the practice yard and its tasks (game/ce.ts, game/yardtask.ts; game_ce.cpp)
  const std::unique_ptr<YardScene> yardScene;
  // (the yard's frame `yardFrame` is in the cranes' block)
  /** the task on the yard now: its run, whether the yard is laid out, the order its steps on foot are offered in, a second try */
  struct YardTaskNow {
    std::unique_ptr<YardTask> run;
    bool laid = false;
    std::map<std::string, double> order;
    bool second = false;
  };
  std::unique_ptr<YardTaskNow> task;
  std::vector<TaskId> tasksLeft;
  int taskCount = 0;
  bool taskFailed = false;
  std::string taskSub;
  double laidTry = 0;
  /** A place on a road wide enough to start with a combination of 16 m, near a start point. */
  SpawnJson truckSpot(const SpawnJson& near);
  /** The next task on the practice yard: lay it out and stand the combination at its start. */
  void nextTask();
  void endTasks();
  /** Put the vehicle somewhere else in the middle of a lesson (the next task, the road after the yard). */
  void placeAgain();
  void taskDone(bool ok);
  /** Judge the task on the yard and say what is to be done. */
  void updateTask(double dt);
  // --- weather, seasons, the windscreen ---
  /** vertical grass on the ground ("No mercy"): render/grass.h (game.ts:290) */
  std::unique_ptr<GrassRenderer> grass;
  /** the weather and the season (game/outdoors.h): made once the map is read (game.ts:443) */
  std::unique_ptr<Outdoors> outdoors;
  /** what the rain is kept out of now: the vehicle's model, a crane's cab (the camera), nothing (game.ts:445) */
  Object3D* shelterOf = nullptr;
  /** the windscreen in rain and snow, and the wiper stalk (render/windscreen.h, game/wipers.h; game.ts:466) */
  std::unique_ptr<WindscreenView> windscreen;
  /**
   * The weather and the season, once a picture: the real weather (or the
   * one chosen) where the player is, rain and snow round the camera but not
   * in the cab, the grip of the road.
   */
  void updateOutdoors(double dt, VehicleModel& model);
};

}  // namespace fs
