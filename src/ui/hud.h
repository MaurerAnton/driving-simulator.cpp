// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/hud.ts @ v1.7.7
/**
 * Menus and the in-game HUD (plain DOM, no framework).
 *
 * One class, as in the original; its functions are spread over hud.cpp (the
 * display while driving), hud_menu.cpp (the main menu, pause, result, log,
 * news …), hud_settings.cpp (the settings and the table of the controls) and
 * hud_touch.cpp (the on-screen controls, the key reminder).
 *
 * What the original's Hud shows of parts that are not in 2.0.0 (docs/STATUS.md,
 * "Later") is left out here, and so are the public functions that serve only those
 * parts:
 *
 *   showReplay, updateReplay, hideReplay,
 *   ReplayBar, ReplayState                     (replays; `onReplay` stays, as the hook it is)
 *   showAssetProgress, setAssetProgress,
 *   hideAssetProgress, setTileVersions,
 *   onTileVersion                              (developer map modes)
 *
 * Where the Hud itself would open such a part, a hook of the original's name stands
 * in its place, empty until the part is there (see "hooks" below): the stage and
 * carousel menus, the learning modes, the arcade panel, the version picker, the map
 * packs.  A menu entry whose part is not there is not shown.
 */
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/task.h"
#include "game/address.h"
#include "game/faultlog.h"
#include "game/settings.h"
#include "game/vehicles.h"
#include "i18n/index.h"
#include "platform/canvas.h"
#include "platform/dom.h"
#include "sim/rules.h"
#include "ui/fuelgauge.h"
#include "ui/hudlayout.h"
#include "ui/notices.h"
#include "ui/viewbuttons.h"
#include "world/lanes.h"
#include "world/types.h"
#include "world/world.h"

namespace fs {

class Input;
class MiniMap;
/** ui/carousel.ts: the vehicle chosen in the menu of rows and where it would start */
struct MenuPreview;
/** ui/arcade.ts: what the arcade panel sets */
struct ArcadeHost;

/** the weathers and seasons one may choose, and the names of each */
inline const std::vector<std::string> WEATHER_CHOICES = {"real", "off", "cloudy", "fog", "drizzle", "rain", "snow", "storm"};
inline const std::vector<std::pair<std::string, std::string>> WEATHER_KEYS = {{"real", "weatherReal"},       {"off", "weatherOff"},   {"cloudy", "weatherCloudy"}, {"fog", "weatherFog"},
                                                                              {"drizzle", "weatherDrizzle"}, {"rain", "weatherRain"}, {"snow", "weatherSnow"},     {"storm", "weatherStorm"}};
inline const std::vector<std::string> SEASON_CHOICES = {"real", "spring", "summer", "autumn", "winter"};
inline const std::vector<std::pair<std::string, std::string>> SEASON_KEYS = {{"real", "seasonReal"}, {"spring", "seasonSpring"}, {"summer", "seasonSummer"}, {"autumn", "seasonAutumn"}, {"winter", "seasonWinter"}};

struct HudState {
  double speedKmh = 0;
  double rpm = 0;
  /** engine speed at which the bar is full (a truck's diesel turns a third as fast as a car's engine) */
  std::optional<double> rpmMax = std::nullopt;
  std::string gear;
  /** km/h; 0 = no limit; -1 = unknown */
  double limit = 0;
  /** the limit is one of a sign with a time on it: what its panel says ("22–6 h") – shown under the roundel ('': it is not) */
  std::string limitWhen;
  int zone = 0;
  /** the next junction (as the rule monitor tells it: RuleContext.junction) */
  std::optional<RuleContext::Junction> junction = std::nullopt;
  std::string navText;
  std::string navSub;
  int navArrow = 0;
  /** a turn is announced (so a notice must not hide it) */
  bool navActive = false;
  std::string tip;
  /** what tapping the tip does on a touch screen: a key code ('' = nothing) */
  std::string tipAction;
  bool showDash = false;
  /** on foot beside the vehicle: no dashboard strip, no reminder of the driving keys */
  bool onFoot = false;
  /** the autopilot is driving the vehicle by itself (a demo or a tour): a stop button is offered */
  bool autopilotDriving = false;
  /** no reminder of the driving keys (the tram, the aeroplane) */
  bool noKeyHints = false;
  int indicator = 0;
  bool hazard = false;
  bool blinkOn = false;
  bool handbrake = false;
  /** the car is being driven against the handbrake */
  bool handbrakeWarn = false;
  bool seatbelt = false;
  bool engineOn = false;
  bool lights = false;
  int points = 0;
  int faults = 0;
  double x = 0;
  double z = 0;
  double yaw = 0;
  /** how full the tank is (0 … 1; none: a vehicle without one) and whether the reserve is reached (game/serviceworld.ts) */
  std::optional<double> fuel = std::nullopt;
  bool fuelLow = false;
};

/** An end of a runway an aeroplane may take off from. */
struct RunwayChoice {
  /** "Flughafen Kassel (EDVK)", "09" */
  std::string field;
  std::string end;
  double x = 0;
  double z = 0;
  double yaw = 0;
};

/** A stop the tram may start at (a place of the stops of the map). */
struct TramStopChoice {
  std::string name;
  std::string town;
  double x = 0;
  double z = 0;
};

struct StartRequest {
  std::string mode;
  int spawn = 0;
  /** start at this address instead of one of the car parks */
  std::optional<AddressHit> address = std::nullopt;
  /** the tram: start at this stop */
  std::optional<TramStopChoice> stop = std::nullopt;
  /** the aeroplane: start at this end of a runway */
  std::optional<RunwayChoice> runway = std::nullopt;
};

struct ResultSummary {
  std::string title;
  bool exam = false;
  bool passed = false;
  std::vector<Violation> log;
  int points = 0;
  double distanceKm = 0;
  double minutes = 0;
  /** more to show under the summary (the crane's tasks as they went: game/craneplay.ts) – empty: nothing */
  dom::El extra;
};

class Hud {
 public:
  Hud(const dom::El& root, Settings& settings, World& world);
  ~Hud();
  Hud(const Hud&) = delete;
  Hud& operator=(const Hud&) = delete;

  /** `(key, vars?) => string`: a text in the player's language, as the panels of other files take it */
  using T = std::function<std::string(const std::string& key, const TrParams& vars)>;

  /** full screen and hiding the display (ui/viewbuttons.ts) */
  std::optional<ViewButtons> view;

  /** the arcade mode's panel: another moment, faster time (set by the game for free drives; null: none) */
  ArcadeHost* arcade = nullptr;
  /** the clock in the HUD was tapped: the game pauses and shows the arcade panel */
  std::function<void()> onArcade = [] {};
  /** after the vehicle went up in flames: the button for a new one */
  std::function<void()> onNewVehicle = [] {};
  /** free drive: the pause menu offers to jump to an address */
  bool canTeleport = false;
  /** fetches the address list when somebody starts typing an address */
  std::function<Task<AddressIndex*>()> loadAddresses = []() -> Task<AddressIndex*> { co_return nullptr; };
  std::function<void(const StartRequest& req)> onStart = [](const StartRequest&) {};
  std::function<void()> onResume = [] {};
  std::function<void()> onQuit = [] {};
  /** watch the drive again (null: nothing recorded yet) */
  std::function<void()> onReplay = nullptr;
  /** the menu of rows: the vehicle chosen and where it would start, for the scene behind (null: the simple menu, no vehicle shown) */
  std::function<void(const MenuPreview* p)> onMenuPreview = [](const MenuPreview*) {};
  /** the reality view (null: there is none – its tile of the pause menu is not shown) */
  std::function<void()> onReality = nullptr;
  /** get out of the vehicle (offered in the pause menu while it stands) */
  std::function<void()> onGetOut = nullptr;
  /** the faults of this and of earlier drives (set by the game) */
  FaultLog* faultLog = nullptr;
  /**
   * The gearbox of the vehicle that is being driven, where it is not the one
   * of the settings: the truck's is automatic, and so is the car's on a
   * demonstration drive.  (The key reminder goes by it, and the touch clutch
   * shows only where there is one to press.)
   * `hud.gearbox = v` of the original is `hud.gearbox(v)`.
   */
  const std::optional<Transmission>& gearbox() const { return _gearbox; }
  void gearbox(std::optional<Transmission> v);
  /** open the full-screen map */
  std::function<void()> onMap = [] {};
  /** plan a demonstration drive (opens the map with its panel for that) */
  std::function<void()> onDemo = [] {};
  /** end a free drive and show its result */
  std::function<void()> onFinish = [] {};
  /** a free drive is running (it has no destination that would end it) */
  bool canFinish = false;
  std::function<void()> onPause = [] {};
  std::function<void(const AddressHit& a)> onTeleport = [](const AddressHit&) {};
  /** where the vehicle is (for the search for addresses: what lies near comes first) */
  std::function<std::optional<AddressIndex::Near>()> where = []() -> std::optional<AddressIndex::Near> { return std::nullopt; };
  std::function<void()> onSettingsChanged = [] {};
  /** the tilt steering was switched (ui/tiltsteer.ts): asked for from the toggle's gesture (iOS asks permission there) */
  std::function<Task<void>(bool on)> onTiltSteer = [](bool) -> Task<void> { co_return; };
  /** the autopilot's stop was pressed: take over / end the drive the autopilot was doing (game.ts endDemo) */
  std::function<void()> onStopAutopilot = [] {};
  /** "Account / server" in the settings and "Play together" in the pause menu (net/online.ts; null: not offered) */
  std::function<void(std::function<void()> back)> onAccount = nullptr;
  std::function<void()> onTogether = nullptr;
  std::vector<Lane*> routeLanes;

  /** the player's arrangement of the HUD's parts (ui/hudlayout.ts) – see `layout` at the end of the class */

  const Lang& lang() const { return settings.lang; }

  // ---------------------------------------------------------------- overlays
  /** The ends of the runways (from the map): the start points of the aeroplane. */
  void setRunways(std::vector<RunwayChoice> list);

  /** The stops trams call at (from the map): the start points of the tram. */
  void setTramStops(std::vector<TramStopChoice> list);

  /** A panel of another part of the game in the place of the menus (an empty El: none) – the server's (ui/netpanels.ts). */
  void showPanel(const dom::El& node);

  bool menuOpen() const { return static_cast<bool>(overlay); }

  void showLoading(const std::string& text);

  /** How far the loading is (0 … 1): the bar of the loading screen fills, and says so in per cent. */
  void setLoadingProgress(double share);

  /** Behind the menu of rows: how far the surroundings of the start place are loaded (null: done). */
  void setMenuLoading(std::optional<double> share);

  void showError(const std::string& text);

  void showMenu(const std::vector<SpawnJson>& spawns);

  /** Draw the main menu anew (a start place was chosen from outside it: the player's location). */
  void redrawMenu();

  /** The map opened from the main menu: the drive is to begin at this place of it (the tram at the stop, the aeroplane at the runway nearest to it). */
  void menuStartAt(const SpawnJson& spot);

  void showSettings(std::function<void()> back);

  void showControls(std::function<void()> back);

  /** What each version brought ("What's new"), newest first. */
  void showNews(std::function<void()> back);

  void showAbout(std::function<void()> back);

  void showIntro(const std::string& title, const std::string& text, std::function<void()> go);

  void showPause();

  /** Free drive: put the car in front of an address. */
  void showTeleport();

  void showResult(const ResultSummary& r);

  /**
   * The log of faults: every fault of this and of earlier drives, newest
   * first, each with its explanation and the rule to read up.
   */
  void showLog(std::function<void()> back);

  void hideOverlay();

  // --------------------------------------------------------------------- HUD
  /** One thing to choose (setChoices). */
  struct Choice {
    std::string id;
    std::string label;
  };

  /**
   * Things to choose from while the vehicle stands (class CE: what is done on
   * foot around truck and trailer; the street-name block above says what is
   * asked).  An empty list takes the panel away.
   * May be called every frame: the panel is rebuilt only when the list changes.
   */
  void setChoices(const std::vector<Choice>& items, std::function<void(const std::string& id)> pick, bool asked = false);

  void setHudVisible(bool v);

  /** A fault or hint of the rule monitor: shown in the block of the street name (`seconds`: 5.5 for a hint, else 8). */
  void toast(const Violation& v);
  void toast(const Violation& v, double seconds);

  void message(const std::string& title, const std::string& sub = "");

  void clearNotices();

  /** The world's moment in the HUD (null: the real one – nothing shown). */
  void setClock(const std::optional<std::string>& text);
  /**
   * The wiper stalk's place as a lamp beside the speed (off: none) – "int",
   * "1", "2" under a wiper sign.  Without a dashboard (null) it is hidden.
   */
  void setWipers(const std::optional<std::string>& mode);

  /** The vehicle went up in flames (true) – the button for a new one – or there is one again (false). */
  void setWrecked(bool on);

  /** on foot the pause menu offers following the real position (liveOffer), which is on or off (liveOn) */
  bool liveOffer = false;
  bool liveOn = false;
  std::function<void()> onLiveLocation = [] {};

  /** another vehicle chosen in the pause menu: the game puts it where the player is */
  std::function<void(const VehicleKind& kind)> onChangeVehicle = [](const VehicleKind&) {};

  /** The vehicles to change to, by their names – the one driven now marked. */
  void showVehiclePicker();

  /** The arcade panel (over the paused drive). */
  void showArcade();

  /** Pictures a second, measured by the game; a negative number hides the display. */
  void setFps(double fps);

  void update(const HudState& s, double now);

  /**
   * The vehicle's screen the map is drawn on instead of the corner of the
   * picture (the car's centre display, seen from the driver's seat) – null:
   * the map is in the corner.  `drawn` is told after each picture.
   */
  struct Screen {
    Canvas canvas;
    std::function<void()> drawn;
  };
  std::optional<Screen> screen;

  // ------------------------------------------------------- on-screen controls
  /**
   * Steering bar, pedals and buttons for driving with the fingers.  They are
   * always built and shown only in touch mode (see setInputMode).
   *
   * Left thumb: steering, indicators.  Right thumb: pedals, gears, handbrake.
   * Everything else is behind "⋯".  The middle of the screen stays free.
   */
  void attachInput(Input& input);

  /** Fingers or keys ('touch' | 'keys')?  Shows the on-screen controls or the key reminder. */
  void setInputMode(const std::string& mode);

  // ------------------------------------------------------------------- hooks
  // Parts of the game that are not in 2.0.0: each hook has the name of what the original
  // calls there and is empty until that part is translated.

  /** the main menu as a stage (ui/stage.ts) and in rows of cards (ui/carousel.ts): Hud.showStage / showCarousel of the original */
  std::function<void(const std::vector<SpawnJson>& spawns)> showStage = nullptr;
  std::function<void(const std::vector<SpawnJson>& spawns)> showCarousel = nullptr;
  /**
   * ui/stage.ts startFromSpot: a place tapped on the map opened from the menu becomes the start – changes `last`;
   * false: nothing fits.  (menuStartAt does nothing without it.)
   */
  std::function<bool(const VehicleKind& kind, Settings::LastStart& last, const SpawnJson& spot, const std::vector<TramStopChoice>* tramStops, const std::vector<RunwayChoice>* runways,
                     const std::string& unnamed)>
      startFromSpot = nullptr;
  /** ui/arcade.ts arcadePanel: the panel for `arcade` */
  std::function<dom::El(ArcadeHost& host)> arcadePanel = nullptr;
  /** ui/versionpicker.ts playVersionButton (an empty El: the site has not that version) and versionSettings (`when`: a moment given as ISO text, written for the reader) */
  std::function<dom::El(const std::string& version, const T& t)> playVersionButton = nullptr;
  std::function<dom::El(const T& t, std::function<std::string(const std::string& iso)> when)> versionSettings = nullptr;
  /** ui/mappacks.ts mapPackSettings */
  std::function<dom::El(Settings& s, const T& t, const std::string& locale)> mapPackSettings = nullptr;

 private:
  const dom::El root;
  Settings& settings;
  World& world;
  dom::El overlay;
  // (what buildHud makes comes before `hud`, which is made by it – see the constructor)
  dom::El navArrow;
  dom::El navText;
  dom::El navSub;
  dom::El navBox;
  dom::El choiceBox;
  bool navLong = false;
  dom::El choiceBtn;
  std::string choiceKey;
  std::function<void(const std::string& id)> choicePick = [](const std::string&) {};
  dom::El roundel;
  std::string roundelText;
  Ctx2D ruler;
  Canvas rulerCanvas;
  dom::El zone;
  dom::El prio;
  /**
   * Notices (faults, hints) take the place of the street name; if several are
   * current, the gravest one shows.
   */
  NoticeBoard notices;
  dom::El navExtra;
  dom::El navMeta;
  std::shared_ptr<const Notice> noticeShown;
  dom::El dash;
  dom::El dashV;
  dom::El lampBrake;
  dom::El lampBelt;
  /** the wiper stalk's place (setWipers): made the first time it is asked for */
  dom::El lampWipers;
  /** the fuel gauge of the dashboard strip (ui/fuelgauge.ts) */
  std::unique_ptr<FuelGauge> fuelGauge;
  dom::El dashG;
  dom::El dashRpm;
  dom::El indL;
  dom::El indR;
  dom::El tip;
  std::string tipAction;
  dom::El score;
  Canvas mapCanvas;
  /** the corner map's box (`.hud .map`) */
  dom::El mapBox;
  /** the corner map's photographs, looking round on it and moving it (minimap.ts) */
  std::unique_ptr<MiniMap> mini;
  dom::El fpsBox;
  /** the touch sheet's starter, and in its place while the engine runs: switching it off */
  dom::El starterBtn;
  dom::El engineOffBtn;
  /** the world's moment when the arcade mode set another one or runs the time faster (a tap opens the arcade panel) */
  dom::El clockBox;
  std::string clockText;
  dom::El wreckBtn;
  /** the autopilot's stop in the ⋯ sheet (shown while the autopilot drives) */
  dom::El autoStopItem;
  dom::El keysBox;
  std::string keysFor;
  Input* input = nullptr;
  /** 'touch' | 'keys' */
  std::string mode = "keys";
  std::unordered_map<std::string, dom::El> lit;
  dom::El moreBox;
  std::optional<HudState> last;
  double lastDom = 0;
  double lastMap = 0;
  std::string lastPrio;
  std::optional<Transmission> _gearbox;
  dom::El clutchPedal;
  /** the stops trams call at, by town and name – null until the map's list of stops is read */
  std::optional<std::vector<TramStopChoice>> tramStops;
  /** draws the menu again (while it is open) */
  std::function<void()> menuRedraw = nullptr;
  /** the ends of the runways of the map's airfields – null until they are read */
  std::optional<std::vector<RunwayChoice>> runways;
  struct LoadingBar {
    dom::El bar;
    dom::El pct;
  };
  std::optional<LoadingBar> loadingBar;
  /** the largest share shown on the loading screen, and behind the menu, since each loading began */
  double loadShown = 0;
  double menuShown = 0;
  bool choiceAsked = false;
  /** what the texts of the HUD itself were last written as (so that the page is touched only when they change) */
  std::string fpsText;
  std::string scoreText;
  std::string tipText;
  /** the HUD itself (made by buildHud, after everything above) */
  dom::El hud;

 public:
  /** the player's arrangement of the HUD's parts (ui/hudlayout.ts) */
  HudLayout layout;

  // --- the native HUD (ui/sdlhud.h: the SDL window paints these values itself) ---
  /** what update() was last given (none: nothing driven yet) */
  const std::optional<HudState>& lastState() const { return last; }
  /** the notice shown in the street name's place now (null: none) */
  const std::shared_ptr<const Notice>& noticeNow() const { return noticeShown; }
  /** the corner map, as update() draws it */
  const Canvas& cornerMap() const { return mapCanvas; }
  /** the HUD is shown (setHudVisible) */
  bool hudShown() const { return hud && hud.style("display") != "none"; }

 private:
  std::string t(std::string_view key, const TrParams& p = {}) const;
  /** `(k, v) => this.t(k, v)`, for the panels of other files */
  T tFn() const;
  double fitRoundel(const std::string& text);
  void setOverlay(const dom::El& node);
  /** A learning mode (ui/learn.ts): the theory exam, the sign trainer, spotting dangers, the lesson log, or their overview ('hub' | 'exam' | 'signs' | 'danger' | 'log'). */
  void showLearn(const std::string& screen, std::function<void()> back);
  struct LearnMode {
    std::string id;
    std::string title;
    std::string desc;
    std::string icon;
    std::function<void()> run;
  };
  /** The learning modes as the menus list them (the "Learn" group). */
  std::vector<LearnMode> learnModes(std::function<void()> back);
  dom::El languagePicker(std::function<void()> redraw, bool inRow = false);
  Task<void> pickLanguage(std::string code, std::function<void()> redraw);
  /** Key caps for the keys with these codes, as printed on the player's keyboard. */
  std::vector<dom::El> keycaps(const std::vector<std::string>& codes);
  dom::El ruleNode(const std::string& id);
  dom::El buildHud();
  void drawMap(const HudState& s, const Canvas& c);
  /** The short reminder of the keys, depending on the gearbox chosen. */
  std::vector<dom::El> keyReminder();
};

}  // namespace fs
