// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/bigmap.ts @ v1.7.7
/**
 * The full-screen map: the whole road network north up, where the car is,
 * where it has been, and – in a free drive – a way to put it anywhere: tap a
 * road, then "put the car here".
 *
 * Drag to move the map, pinch or use the wheel to zoom.  Everything works
 * with a finger, a mouse or the keyboard (arrows, + and -).
 *
 * 2.0: one map for all regions (docs/WORLD-MAP.md).  With a WorldLayer
 * (ui/worldlayer.h) the map zooms out beyond the loaded region: the countries'
 * outlines and every region there is, a tap picks one, "Open this map" hands
 * it to the game (BigMapHost::onOpenRegion).  Everything of it is marked
 * `// 2.0:`; without a layer the map is the original's.
 *
 * Not translated yet, and so behind hooks (ui/maphooks.h): the aerial
 * photographs, the trams and buses (ui/transitmap.ts), the roadworks and
 * demonstrations, the fuels' names; ui/lessonroutes.ts is another translator's.
 */
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/task.h"
#include "game/address.h"
#include "game/tour.h"
#include "game/tourlog.h"
#include "game/track.h"
#include "i18n/index.h"
#include "platform/canvas.h"
#include "platform/dom.h"
#include "ui/addresspicker.h"
#include "ui/maphooks.h"
#include "ui/maplayers.h"
#include "ui/mapstyle.h"
#include "ui/pathdraw.h"
#include "ui/placesearch.h"
#include "ui/worldlayer.h"
#include "world/netindex.h"
#include "world/regions.h"
#include "world/types.h"
#include "world/world.h"

namespace fs {

/** A planned tour: what it comes to, and its line on the map (x, z, x, z, …). */
struct TourPreview {
  TourPlan plan;
  std::vector<float> line;
  /** the way (world/routegraph.ts) it begins behind: the road the car stood on */
  int from = 0;
  /** where it ends (a round trip: where it started) */
  SpawnJson end;
};

/**
 * What the map asks of the game.  Members the original marks with `?` may be left
 * empty (`if (host.transit) …`); the others must be set.  Lists and objects the game
 * keeps are handed over as pointers (null: none) – they must hold until the map asks again.
 */
struct BigMapHost {
  World* world = nullptr;
  /** the roads of the whole map, where the road network itself is only loaded around the car (null: not read yet, or a map in one piece) */
  std::function<NetIndex*()> net;
  std::function<std::string()> lang;
  /** the car: position and yaw (0 = north, positive = to the left) */
  struct Car {
    double x = 0;
    double z = 0;
    double yaw = 0;
  };
  std::function<Car()> car;
  DrivenPath* path = nullptr;
  /** the place on a road next to a point of the map where the car could stand, or null */
  std::function<std::optional<SpawnJson>(double x, double z)> spotAt;
  /** may the car be put somewhere else (a free drive)? */
  std::function<bool()> canTeleport;
  std::function<void(const SpawnJson& spot)> teleport;
  /** ? opened from the main menu, before a drive: a place tapped is offered as where the drive is to begin (`teleport`) – nothing to plan there yet */
  std::function<bool()> menu = nullptr;
  /** ? the fuel stations and car washes, the emergency vehicles of the traffic – and a fuel's price at a station (today's, or a typical one) */
  std::function<const std::vector<MapService>*()> services = nullptr;
  std::function<const std::vector<MapEmergency>*()> emergencies = nullptr;
  struct FuelPrice {
    double price = 0;
    bool live = false;
  };
  std::function<FuelPrice(const MapService& s, const std::string& fuel)> fuelPrice = nullptr;
  /** ? the name of the service the prices come from (shown under them when they are today's) */
  std::function<std::string()> priceSource = nullptr;
  /** `{ kind: 'em'; id } | { kind: 'event'; ev }` */
  struct WatchPoint {
    /** 'em' | 'event' */
    std::string kind;
    int id = 0;
    std::optional<RoadEventInfo> ev;
  };
  /** ? watch an emergency vehicle, a demonstration or a site of roadworks */
  std::function<void(const WatchPoint& what)> watchPoint = nullptr;
  /** ? the stops of trams and buses (for the search field) */
  std::function<const std::vector<StopSource>*()> stops = nullptr;
  /** ? the roadworks and demonstrations of the day (sim/roadevents.ts) – drawn with their icons, the march's way in red */
  std::function<const std::vector<RoadEventInfo>*()> events = nullptr;
  /** the address list, for adding a place by its address (null: there is none) */
  std::function<Task<AddressIndex*>()> loadAddresses;
  /** the place on the road in front of an address, or null */
  std::function<std::optional<SpawnJson>(const AddressHit& hit)> spotOf;
  /**
   * A drive to these places, in this order – or along a tour that was planned
   * (planTour).  `self`: the player drives and is shown the way; otherwise
   * the car drives by itself.  (`tour`: null, or one that holds for the call.)
   */
  std::function<void(const std::vector<SpawnJson>& stops, bool self, const TourPreview* tour)> startTrip;
  /** ? start a set route (game/routepresets.ts) from where the vehicle stands */
  std::function<void(const std::string& id)> startPreset = nullptr;
  /**
   * A tour of about `metres` through as many different streets as possible,
   * from where the car stands back to there – or on to `end`.  Null: none found.
   */
  std::function<Task<std::optional<TourPreview>>(double metres, std::optional<SpawnJson> end, std::function<void(double share)> progress)> planTour;
  /** are the roads of the whole map known (for planning ways through it)? */
  std::function<bool()> roadsKnown;
  /** what a tour is to lead along (kept in the settings) */
  std::function<TourPrefs()> tourPrefs;
  std::function<void(const TourPrefs& p)> setTourPrefs;
  /**
   * ? Where trams can go: the tracks of trams and regional trams (x, z, … per
   * piece) and their stops – `version` changes when the tracks were read anew.
   * Null while they are not read.
   */
  struct Tramways {
    int version = 0;
    std::vector<std::vector<float>> tracks;
    struct Stop {
      double x = 0;
      double z = 0;
      std::string name;
    };
    std::vector<Stop> stops;
  };
  std::function<const Tramways*()> tramways = nullptr;
  /** is a drive to places or a tour under way? – and a way to end it */
  std::function<bool()> tripActive;
  std::function<void()> endTrip;
  /** latitude and longitude of a point of the map */
  std::function<std::pair<double, double>(double x, double z)> latLon;
  /** aerial photographs under the roads: has the player switched them on? (they come from a server of the land survey) */
  std::function<bool()> aerial;
  std::function<void(bool on)> setAerial;
  /** ? the trams and buses of the timetables and the stops, for the map (src/ui/transitmap.ts) – null: none */
  std::function<TransitSource*()> transit = nullptr;
  /** ? watch one of them: the camera goes with it (in a free drive) */
  std::function<void(const WatchRequest& v)> watch = nullptr;
  /** ? the roads as the route planner sees them, for laying a way drawn on the map onto them (null: not read yet) */
  std::function<const RoadSource*()> roads = nullptr;
  /** ? the aeroplane is flown: it is put somewhere in the air, `height` metres above the ground there */
  std::function<bool()> flying = nullptr;
  std::function<void(const SpawnJson& spot, double height)> teleportAir = nullptr;
  /** ? the area of the world in memory, where a car may be put down: the line round it (x1, z1, x2, z2 per side) – null: not shown */
  std::function<const std::vector<float>*()> loadedArea = nullptr;
  /** ? may a car be put down at (x, z) now ('ok'; 'outside': not on the loaded area; 'no': not now) – and put it there (game/spawncar.ts) */
  std::function<std::string(double x, double z)> canSpawnCar = nullptr;
  std::function<void(double x, double z)> spawnCar = nullptr;
  std::function<void()> close;

  // 2.0: one map for all regions (used only when the map has a WorldLayer)
  /** ? the id of the region that is loaded – '' (WorldLayer::SITE): the site's own map; left empty: the site's own map */
  std::function<std::string()> region = nullptr;
  /** ? the player chose another region on the map ("Open this map"): save it in the settings and load it */
  std::function<void(const Region& r)> onOpenRegion = nullptr;
};

class BigMap {
 public:
  /** (2.0: `world`: the layer of all regions – null: the map of the loaded region alone, as in 1.7.7) */
  explicit BigMap(BigMapHost host, std::shared_ptr<WorldLayer> world = nullptr);
  ~BigMap();
  BigMap(const BigMap&) = delete;
  BigMap& operator=(const BigMap&) = delete;

  /** (the original's getter `isOpen`) */
  bool isOpen() const { return static_cast<bool>(node); }

  /** `close`: a close-up of where the car is (for comparing the model with the aerial photograph). */
  void open(const dom::El& parent, bool close = false);

  /** Open the panel for planning a drive (the map must be open). */
  void plan();

  void close();

  // 2.0: ------------------------------------------------------------------------------
  /** the layer of all regions, given later than the map was made (null: none) */
  void setWorld(std::shared_ptr<WorldLayer> world);
  /** Every region in view – what the globe in the map's bar does; again: back to the view before.  (The map must be open.) */
  void showAllRegions();
  /** Show a region on the world and select it (its card opens); the loaded one: back into it.  (The map must be open.) */
  void showRegion(const std::string& id);
  /** the region the player has selected on the map (null: none) */
  const Region* chosenRegion() const;

 private:
  struct Pointer {
    double x = 0;
    double y = 0;
  };
  /** one line of roads to stroke: the points of its pieces, one after the other (what the original keeps as a Path2D) */
  struct RoadPath {
    std::vector<double> xy;
    /** where each piece begins in `xy` (index of its first number) */
    std::vector<size_t> starts;
    void moveTo(double x, double z) {
      starts.push_back(xy.size());
      xy.push_back(x);
      xy.push_back(z);
    }
    void lineTo(double x, double z) {
      xy.push_back(x);
      xy.push_back(z);
    }
    /** `g.stroke(path)` */
    void stroke(const Ctx2D& g) const;
  };

  BigMapHost host;
  dom::El node;
  Canvas canvas;
  dom::El spotBar;
  /** which of the layers are shown (maplayers.ts) */
  LayerFlags layers = loadLayers();
  double cx = 0;
  double cz = 0;
  double scale = 0.25;
  bool showPath = true;
  std::optional<SpawnJson> spot;
  /** the places of a demonstration drive being planned (kept while the map is closed) */
  std::vector<SpawnJson> stops;
  dom::El planBox;
  /** the tour that was planned for the places as they are now, and the length asked for (km) */
  std::optional<TourPreview> tour;
  double tourKm = 10;
  std::string tourNote;
  /** the line of a tour driven before, shown on the map */
  std::optional<std::vector<float>> pastTour;
  /** the tram tracks as one path (world coordinates), and for which version of them it was made */
  std::optional<RoadPath> tramPath;
  int tramVersion = -1;
  /** a tour is being worked out (the button waits) */
  bool planning = false;
  dom::El scaleBar;
  dom::El scaleText;
  dom::El credit;
  int frame = 0;
  /** (in the order the fingers came down, as the original's Map keeps them) */
  std::vector<std::pair<int, Pointer>> pointers;
  double moved = 0;
  double pinch = 0;
  /** (null until the aerial photographs are translated: ui/maphooks.h) */
  std::unique_ptr<AerialCache> photos;
  /** the trams and buses and the stops (transitmap.ts – null until it is translated), drawing a way (pathdraw.ts), the grip of the panel for planning */
  std::unique_ptr<TransitLayer> transit;
  std::unique_ptr<PathDrawer> drawer;
  std::function<void()> ungrip;
  /** the height the aeroplane is put at (m above the ground) */
  double airHeight = 600;
  /** the listeners on the window (resize, keydown), to take them away again */
  int onResize = 0;
  int onKey = 0;
  /** (what waits for something – a tour being planned, the player's position – looks here whether the map is still there) */
  std::shared_ptr<bool> alive = std::make_shared<bool>(true);

  /** the fields of the two address pickers (the map's search, the panel for planning): their callbacks empty them */
  dom::El searchInput;
  dom::El planInput;

  std::string t(const std::string& key, const TrParams& params = {}) const;
  Pointer* pointer(int id);
  Task<AddressSearchOf<PlaceHit>> loadSearch();
  Task<AddressSearchOf<AddressHit>> loadPlanSearch();
  void fillPlan();
  void planChanged();
  Task<void> planIt(dom::El km, dom::El planBtn, dom::El tourBox);
  Task<void> locateMe(dom::El me);
  /** Move and zoom the map so that a line (x, z, x, z, …) is in view. */
  void showAll(const std::vector<float>& line);
  void key(dom::Event& e);
  /** Zoom by a factor; the point of the screen under (px, py) keeps its place (default: the middle). */
  void zoom(double factor, std::optional<double> px = std::nullopt, std::optional<double> py = std::nullopt);
  /** A tap at a point of the screen: the nearest place on a road. */
  void choose(double px, double py);
  /** The lines of public transport as the search may find them. */
  std::vector<LineSource> lineSources();
  /** The map to a place (a search hit, the player's position): centred, zoomed to `scale` at least, its place panel open. */
  void goTo(double x, double z, double scale, const std::string& name);
  /** A short line in the place panel (a position that could not be found). */
  void note(const std::string& text);
  /** The place panel for a point of the map: the road there, and what can be done (go there, plan a drive, start from it). */
  void showSpot(double x, double z, const std::string& name = "");
  /** The roadworks (an orange sign) and demonstrations (a red one, the march's way in red dashes) in view. */
  void drawEvents(const Ctx2D& g, double W, double H, double s);
  /** The roadworks or demonstration whose sign is under a point of the map (null: none). */
  std::optional<RoadEventInfo> eventAt(double px, double py, double W, double H);
  void redraw();
  void draw();
  void drawScale(double W, double s, bool aerial);

  // 2.0: one map for all regions ----------------------------------------------------------
  std::shared_ptr<WorldLayer> worldLayer;
  /** the region the player has tapped (its card is open) – by its id, which is '' for the site's own map */
  std::optional<std::string> chosen;
  /** the view the globe button left (it goes back there) */
  struct View {
    double cx = 0;
    double cz = 0;
    double scale = 0;
  };
  std::optional<View> worldBack;
  /** the card of a region (beside the place panel, in the column of the map's controls), the globe in the bar */
  dom::El regionBar;
  dom::El worldBtn;
  /** was the last picture the world's (below WorldLayer::WORLD_SCALE)? – the card follows it */
  bool worldFar = false;
  /** the map waits for the layer to be loaded (once) */
  bool worldAwaited = false;

  /** is there a layer with regions to show? */
  bool worldOn() const;
  /** the farthest out the map goes: the original's MIN_SCALE, WorldLayer::MIN_SCALE with the world */
  double minScale() const;
  /** the id of the loaded region (host.region) */
  std::string currentRegion() const;
  /** the map's view, for the layer */
  MapView view() const;
  /** [minX, minZ, maxX, maxZ] the middle of the map may lie in: the loaded map, or everything the world shows */
  std::array<double, 4> reach() const;
  Task<void> awaitWorld();
  /** a tap at (x, y) of the picture, for the world: true when it was the world's (a region chosen, back into the loaded one) */
  bool worldTap(double x, double y);
  void chooseRegion(const Region& r);
  /** back into the loaded region: the view the globe left, else where the car is */
  void backToMap();
  /** the card: the chosen region with "Open this map", far out also the way back */
  void fillRegion();
  /** the regions as the search field finds them */
  std::vector<RegionSource> regionSources() const;

  // 2.0: the map's look (ui/mapstyle.h) -------------------------------------------------------
  /** the style and the online search, as the player chose them (kept on this device) */
  MapPrefs prefs = loadMapPrefs();
  /** the button of the bar that switches between the dark and the light map */
  dom::El styleBtn;
  const MapStyle& style() const { return mapStyleOf(prefs.style); }
  /** the light map or the dark one (kept) */
  void setStyle(const std::string& name);
};

}  // namespace fs
