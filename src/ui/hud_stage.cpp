// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/hud.ts @ v1.7.7 – the main menu in rows (showCarousel) and as a stage (showStage)
#include <memory>
#include <string>
#include <vector>

#include "game/releases.h"
#include "game/routepresets.h"
#include "ui/addresspicker.h"
#include "ui/carousel.h"
#include "ui/geolocate.h"
#include "ui/hud.h"
#include "ui/hud_parts.h"
#include "ui/lessonroutes.h"
#include "ui/stage.h"

namespace fs {

using dom::El;
using dom::el;

void Hud::showCarousel(const std::vector<SpawnJson>& spawnList) {
  El o = el("div", "overlay clear");
  const auto spawns = std::make_shared<const std::vector<SpawnJson>>(spawnList);
  const std::function<void()> redraw = [this, spawns] { showMenu(*spawns); };
  CarouselContext c;
  c.lang = lang();
  c.t = tFn();
  c.settings = &settings;
  c.spawns = *spawns;
  c.tramStops = tramStops ? &*tramStops : nullptr;
  c.runways = runways ? &*runways : nullptr;
  c.hasYard = world.json.yard.has_value();
  c.language = languagePicker(redraw);
  c.address = [this, spawns](std::function<void(const AddressHit&)> pick) {
    return addressPicker(AddressPickerOptions<AddressHit>{
                             .placeholder = t("addressPlaceholder"),
                             .loading = t("realityLoading"),
                             .none = t("addressNone"),
                             .load = [this] { return hudParts::searchOf(loadAddresses); },
                             .pick = std::move(pick),
                             .near = [this, spawns]() -> std::optional<AddressIndex::Near> {
                               const int i = settings.lastStart.spawn;
                               if (i < 0 || static_cast<size_t>(i) >= spawns->size()) return std::nullopt;
                               return AddressIndex::Near{(*spawns)[static_cast<size_t>(i)].x, (*spawns)[static_cast<size_t>(i)].z};
                             },
                         })
        .node;
  };
  c.tools = {
      {t("settings"), [this, redraw] { showSettings(redraw); }}, {t("controls"), [this, redraw] { showControls(redraw); }}, {t("logTitle"), [this, redraw] { showLog(redraw); }},
      {t("news"), [this, redraw] { showNews(redraw); }},         {t("about"), [this, redraw] { showAbout(redraw); }},
  };
  for (const LearnMode& m : learnModes(redraw)) c.learn.push_back({.title = m.title, .desc = m.desc, .run = m.run});
  c.version = t("newsVersion", {{"v", VERSION}});
  c.start = [this](const StartRequest& req) { onStart(req); };
  c.preview = [this](const MenuPreview& p) { onMenuPreview(&p); };
  c.redraw = redraw;
  o.append(carouselMenu(c));
  setOverlay(o);
  menuRedraw = redraw;
}

void Hud::showStage(const std::vector<SpawnJson>& spawnList) {
  El o = el("div", "overlay clear");
  const auto spawns = std::make_shared<const std::vector<SpawnJson>>(spawnList);
  const std::function<void()> redraw = [this, spawns] { showMenu(*spawns); };
  StageContext c;
  c.lang = lang();
  c.t = tFn();
  c.settings = &settings;
  c.spawns = *spawns;
  c.tramStops = tramStops ? &*tramStops : nullptr;
  c.runways = runways ? &*runways : nullptr;
  c.hasYard = world.json.yard.has_value();
  c.language = languagePicker(redraw);
  c.address = [this, spawns](std::function<void(const AddressHit&)> pick) {
    return addressPicker(AddressPickerOptions<AddressHit>{
                             .placeholder = t("addressPlaceholder"),
                             .loading = t("realityLoading"),
                             .none = t("addressNone"),
                             .load = [this] { return hudParts::searchOf(loadAddresses); },
                             .pick = std::move(pick),
                             .near = [this, spawns]() -> std::optional<AddressIndex::Near> {
                               const int i = settings.lastStart.spawn;
                               if (i < 0 || static_cast<size_t>(i) >= spawns->size()) return std::nullopt;
                               return AddressIndex::Near{(*spawns)[static_cast<size_t>(i)].x, (*spawns)[static_cast<size_t>(i)].z};
                             },
                         })
        .node;
  };
  c.pages.settings = [this, redraw] { showSettings(redraw); };
  c.pages.news = [this, redraw] { showNews(redraw); };
  c.pages.map = [this] { onMap(); };
  c.pages.controls = [this, redraw] { showControls(redraw); };
  c.pages.log = [this, redraw] { showLog(redraw); };
  c.pages.about = [this, redraw] { showAbout(redraw); };
  c.here = [this]() -> Task<StageContext::Here> {
    const WorldMetaJson& m = world.json.meta;
    const LocateResult r = co_await locate(MapBounds{m.minX, m.maxX, m.minZ, m.maxZ});
    if (r.ok) co_return StageContext::Here{.hit = AddressHit{.label = t("myLocation"), .street = "", .x = r.x, .z = r.z, .exact = false}, .error = ""};
    co_return StageContext::Here{.hit = std::nullopt, .error = t(r.why == "outside" ? "myLocationOutside" : r.why == "denied" ? "myLocationDenied" : "myLocationNone")};
  };
  // the theory lessons: difficult situations shown on the practice yard (game/theorymode.ts) – for everything but the aeroplane
  std::vector<VehicleKind> notPlane;
  for (const VehicleKind& k : VEHICLE_KINDS)
    if (k != "plane") notPlane.push_back(k);
  c.extraModes.push_back(StageExtraMode{.id = "theory", .label = t("theory.title"), .icon = "", .kinds = notPlane, .yard = true});
  // the set routes (game/routepresets.ts): a page of them – a tap starts the drive at the first place of one
  c.extraModes.push_back(StageExtraMode{.id = "routes",
                                        .label = t("routes.title"),
                                        .icon = R"(<path d="M5 20c0-6 14-4 14-10a4 4 0 0 0-8 0"/><circle cx="5" cy="20" r="1.5"/><circle cx="11" cy="10" r="1.5"/>)",
                                        .kinds = ROAD_VEHICLE_KINDS,
                                        .run = [this, spawns] {
                                          setOverlay(routePresetPage(
                                              tFn(), [this](const std::string& id) { onStart(StartRequest{.mode = routeMode(id), .spawn = 0}); },
                                              [this, spawns] { showMenu(*spawns); }));
                                        }});
  // the forklift's work on its yard (game/forkliftplay.ts: FORK_WORK_MODE) – its free drive begins at the place chosen
  c.extraModes.push_back(StageExtraMode{.id = "forklift",
                                        .label = t("forkWork"),
                                        .icon = R"(<rect x="7" y="6" width="11" height="8" rx="1"/><path d="M3 18h18M6 14v4M12.5 14v4M19 14v4M2 11h4"/>)",
                                        .kinds = std::vector<VehicleKind>{"forklift"}});
  // the learning modes (ui/learn.ts): the "Learn" group – the exam, the signs, spotting dangers, the lesson log
  for (const LearnMode& m : learnModes(redraw)) c.extraModes.push_back(StageExtraMode{.id = m.id, .label = m.title, .icon = m.icon, .run = m.run, .learn = true});
  c.version = t("newsVersion", {{"v", VERSION}});
  c.start = [this](const StartRequest& req) { onStart(req); };
  c.preview = [this](const MenuPreview* p) { onMenuPreview(p); };
  c.redraw = redraw;
  o.append(stageMenu(c));
  setOverlay(o);
  menuRedraw = redraw;
}

}  // namespace fs
