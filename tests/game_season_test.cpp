// LINK: game render engine world core platform geo
// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The seasons: what the date (the game's clock) makes of the trees, the
 * ground and the snow; the seasons the player picks; summer as the land was
 * always drawn; the colours of the ground, the crowns and the snow (the pure
 * mirrors of the shaders); snow from the real weather; grip with snow on the
 * road; and the materials taught the seasons.
 *
 * Origin: tests/game/season.test.ts @ v1.7.7 (the shaders are the ones the original's patch leaves,
 * captured into render/glsl_gen.cpp: what a material is given is looked at here).
 */
#include <cassert>
#include <cmath>
#include <cstdio>
#include <set>
#include <string>

#include "core/date.h"
#include "core/geom.h"
#include "core/js.h"
#include "game/clock.h"
#include "game/weather.h"
#include "platform/plat.h"
#include "render/materials.h"
#include "render/season.h"
#include "render/snowtracks.h"
#include "world/cover.h"
#include "world/lanes.h"
#include "world/world.h"

using namespace fs;

/** `new Date(year, month, day)`: the beginning of a day of the device's own calendar, in milliseconds since 1970 */
static double localDay(int year, int month, int day) {
  const double utc = utcMs(year, month, day);
  double ms = utc + plat::timezoneOffsetMin(utc) * 60000;
  ms = utc + plat::timezoneOffsetMin(ms) * 60000;
  return ms;
}

/** linear RGB of an sRGB hex colour, as the shaders see it */
static RGB lin(uint32_t hex) {
  const Color c(hex);
  return {c.r, c.g, c.b};
}
static std::array<int, 3> rgb(uint32_t hex) { return {static_cast<int>((hex >> 16) & 255), static_cast<int>((hex >> 8) & 255), static_cast<int>(hex & 255)}; }
static double saturation(uint32_t hex) {
  const auto c = rgb(hex);
  const int mx = std::max({c[0], c[1], c[2]});
  const int mn = std::min({c[0], c[1], c[2]});
  return static_cast<double>(mx - mn) / mx;
}
static bool contains(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

static WeatherState fromMeteo(double code, std::optional<double> cloud, double temperature, double snowDepth) {
  OpenMeteoCurrent c;
  c.weather_code = code;
  c.cloud_cover = cloud;
  c.temperature_2m = temperature;
  c.snow_depth = snowDepth;
  return weatherFromOpenMeteo(c);
}

int main() {
  // the season by the date: leaves, blossom, autumn colours, snow – and its name is the clock's
  {
    const SeasonState jan = seasonAt(localDay(2027, 0, 15));
    assert(jan.season == "winter");
    assert(jan.leaves == 0);
    assert(jan.dormant == 1);
    assert(jan.snow == 1);   // a typical winter
    assert(seasonAt(localDay(2027, 0, 15), false).snow == 0);   // unless the real weather says how much
    const SeasonState apr = seasonAt(localDay(2027, 3, 22));
    assert(apr.season == "spring");
    assert(apr.blossom > 0.8 && apr.spring > 0.8);
    assert(apr.leaves > 0.05 && apr.leaves < 0.8);
    assert(apr.snow == 0);
    const SeasonState jul = seasonAt(localDay(2027, 6, 15));
    assert((jul == SeasonState{.season = "summer", .leaves = 1, .spring = 0, .blossom = 0, .autumn = 0, .dormant = 0, .snow = 0, .roadSnow = 0}));
    const SeasonState oct = seasonAt(localDay(2026, 9, 20));
    assert(oct.season == "autumn");
    assert(oct.autumn > 0.8 && oct.leaves > 0.8);
    const SeasonState lateNov = seasonAt(localDay(2026, 10, 25));
    assert(lateNov.leaves == 0);
    assert(lateNov.autumn == 1);
    // every day of a year: the name is the game clock's, the values within 0 … 1
    for (int d = 0; d < 365; d++) {
      const double date = localDay(2027, 0, 1 + d);
      const SeasonState s = seasonAt(date);
      assert(s.season == seasonOf(date));
      assert(dayOfYear(date) == d + 1);
      for (const double v : {s.leaves, s.spring, s.blossom, s.autumn, s.dormant, s.snow, s.roadSnow}) assert(v >= 0 && v <= 1);
    }
  }

  // summer is the land as it was always drawn
  {
    const std::vector<uint32_t> cover(COVER_COLORS, COVER_COLORS + 18);
    assert(coverPalette(SEASON_PRESETS("summer")) == cover);
    assert(coverPalette(seasonAt(localDay(2027, 6, 15))) == cover);
    for (const double r1 : {0.0, 0.4, 0.9}) {
      for (const char* kind : {"deciduous", "orchard", "evergreen"}) {
        const CrownLook c = crownLook(SEASON_PRESETS("summer"), kind, r1, 1 - r1);
        assert((c.tint == RGB{1, 1, 1}));
        assert(c.cover == 1);
      }
    }
    for (const SeasonSetting& s : SEASON_SETTINGS) assert(isSeasonSetting(s));
    assert(!isSeasonSetting("monsoon"));
  }

  // spring: fresher green, young crops on the fields, fruit trees in blossom
  {
    const SeasonState sp = SEASON_PRESETS("spring");
    const auto meadow = rgb(coverColor(COVER_MEADOW, sp));
    const auto summerMeadow = rgb(COVER_COLORS[COVER_MEADOW]);
    assert(meadow[1] > summerMeadow[1]);   // greener
    const auto field = rgb(coverColor(COVER_FARMLAND, sp));
    assert(field[1] > field[0]);   // young crops
    const CrownLook bloom = crownLook(sp, "orchard", 0.5, 0.5, true);
    assert(bloom.tint[2] > bloom.tint[1] && bloom.tint[0] > bloom.tint[1] && bloom.tint[1] > 2);   // white-pink
    assert(crownLook(sp, "orchard", 0.5, 0.5, false).tint == crownLook(sp, "deciduous", 0.5, 0.5).tint);   // between the blossoms: the leaves
    assert(crownLook(sp, "deciduous", 0.9, 0.5).tint[1] > 2);   // now and then another tree blooms
  }

  // autumn: yellow, orange, red and brown crowns, each tree its own; ploughed fields; streets and water stay
  {
    const SeasonState au = SEASON_PRESETS("autumn");
    std::set<RGB> tints;
    for (const double r2 : {0.1, 0.5, 0.75, 0.95}) {
      const RGB t = crownLook(au, "deciduous", 0.1, r2).tint;
      assert(t[0] > t[1] * 1.3);   // turned
      tints.insert(t);
    }
    assert(tints.size() == 4);   // four colours
    const auto field = rgb(coverColor(COVER_FARMLAND, au));
    const auto summerField = rgb(COVER_COLORS[COVER_FARMLAND]);
    assert(static_cast<double>(field[0]) / field[1] > static_cast<double>(summerField[0]) / summerField[1] && field[1] < summerField[1]);   // browner
    for (const char* season : {"spring", "autumn", "winter"}) {
      for (const int cls : {COVER_ASPHALT, COVER_PAVED, COVER_WATER, COVER_COMMERCIAL, COVER_INDUSTRIAL, COVER_RAILWAY}) assert(coverColor(cls, SEASON_PRESETS(season)) == COVER_COLORS[cls]);
    }
    assert((crownLook(au, "evergreen", 0.3, 0.3).tint == RGB{1, 1, 1}));   // conifers keep their green
  }

  // winter: bare crowns (the twigs, seen through), dull grass, brown fields, leaf litter on the forest floor
  {
    const SeasonState wi = SEASON_PRESETS("winter");
    for (const char* kind : {"deciduous", "orchard"}) {
      const CrownLook c = crownLook(wi, kind, 0.5, 0.5);
      assert(c.cover == 1);   // the twigs: a soft grey-brown mass, not speckles against the sky
      assert(c.tint[0] > c.tint[1]);   // grey-brown twigs
    }
    assert(crownLook(wi, "evergreen", 0.5, 0.5).cover == 1);
    assert(saturation(coverColor(COVER_MEADOW, wi)) < saturation(COVER_COLORS[COVER_MEADOW]));   // duller grass
    const auto floor = rgb(coverColor(COVER_FOREST, wi));
    assert(floor[0] > rgb(COVER_COLORS[COVER_FOREST])[0]);   // litter
    // the pure function and the palette agree
    const RGB g = seasonGround(lin(COVER_COLORS[COVER_MEADOW]), wi);
    Color c;
    c.setRGB(g[0], g[1], g[2]);
    assert(c.getHex() == coverColor(COVER_MEADOW, wi));
  }

  // November: bare crowns read as light grey-brown twigs, lit evenly – not black balls; summer stays green
  {
    // (late November in Baunatal: the leaves are down)
    const SeasonState nov = seasonAt(localDay(2026, 10, 18));
    assert(nov.leaves < 0.1);
    // the twigs' colour: a light grey-brown – sRGB luminance well above the summer crown's green, below pale grey
    const auto [r, g, b] = rgb(BARE_CROWN);
    const double luma = (0.2126 * r + 0.7152 * g + 0.0722 * b) / 255;
    assert(luma > 0.5 && luma < 0.75);
    assert(r >= g && g >= b && r - b < 40);   // grey-brown, not saturated
    const auto green = rgb(0x4c7a36);
    assert(luma > (0.2126 * green[0] + 0.7152 * green[1] + 0.0722 * green[2]) / 255 + 0.15);   // lighter than the leaves
    const CrownLook c = crownLook(nov, "deciduous", 0.5, 0.5);
    const RGB want = lin(BARE_CROWN);
    for (size_t i = 0; i < 3; i++) assert(std::abs(c.tint[i] * lin(0x4c7a36)[i] - want[i]) < 0.02);   // crownLook mirrors the twig colour
    // and the light: the bare crown's normal bends towards the sky, so its side away from a low sun is not black
    assert(BARE_FLAT >= 0.6 && BARE_FLAT <= 1);
    SeasonLook look;
    MaterialPtr tree = lambert({.vertexColors = true});
    MaterialPtr conifer = lambert({.vertexColors = true});
    look.patch(*tree, "deciduous");
    look.patch(*conifer, "evergreen");
    const std::string& fs = tree->patch->fragmentSource;
    const size_t bend = fs.find("normal = normalize(mix(normal, fsUpB, vFsBare");
    assert(bend != std::string::npos);
    assert(bend > fs.find("#include <normal_fragment_maps>") && bend < fs.find("#include <lights_fragment_begin>"));   // bent after the normal is known, before the light
    assert(bend > fs.find("fsSnowCover("));   // the snow still sees the crown as it is
    assert(contains(tree->patch->vertexSource, "vFsBare = (1.0 - fsHas) * fsCrown;"));
    assert(contains(conifer->patch->vertexSource, "vFsBare = 0.0;"));   // conifers keep their shading
    // summer: full leaves, nothing bare, the summer green as always
    const CrownLook su = crownLook(seasonAt(localDay(2027, 6, 15)), "deciduous", 0.5, 0.5);
    assert((su.tint == RGB{1, 1, 1}));
  }

  // snow lies on what faces the sky, in patches where it is thin
  {
    assert(snowCover(0, 1, 0.55, 0.8, 0.9) == 0);
    for (const double n : {0.0, 0.3, 0.7, 1.0}) assert(snowCover(1, 1, 0.55, 0.8, n) == 1);   // all white
    assert(snowCover(1, 0, 0.55, 0.8, 0.5) == 0);     // not on a wall
    assert(snowCover(1, 0.7, 0.4, 0.65, 0.5) == 1);   // a pitched roof holds it
    assert(snowCover(1, 0.7, 0.55, 0.8, 0.5) < 1);    // a steep bank less
    std::vector<double> thin;
    for (const double n : {0.1, 0.3, 0.5, 0.7, 0.9}) thin.push_back(snowCover(0.5, 1, 0.55, 0.8, n));
    assert(thin[0] == 0 && thin[4] == 1);   // patches
    for (size_t i = 1; i < thin.size(); i++) assert(thin[i] >= thin[i - 1]);
  }

  // the season to show: chosen or by the game's day; snow from the real weather, falling snow settles; grip on a snowy road
  {
    const double jan = localDay(2027, 0, 15);
    const SeasonState winter = seasonFor("winter", nullptr, localDay(2027, 6, 1));
    assert(winter.snow == 1);
    assert(winter.roadSnow == 0.7);
    assert(winter.season == "winter");   // chosen: whatever the date
    assert(gripFactor(*FIXED_WEATHER("clear"), winter.roadSnow) <= 0.65);
    assert(seasonFor("real", nullptr, jan).snow == 1);   // no real weather: a typical winter
    const WeatherState green = fromMeteo(3, 90, 2, 0);
    assert(green.source == "real");
    assert(seasonFor("real", &green, jan).snow == 0);   // the real weather says: no snow lies
    const WeatherState white = fromMeteo(3, 90, -2, 0.05);
    const SeasonState s = seasonFor("real", &white, jan);
    assert(s.snow == 1);
    assert(std::abs(s.roadSnow - 0.7) < 1e-12);
    const WeatherState thawing = fromMeteo(3, std::nullopt, 6, 0.05);
    const SeasonState thaw = seasonFor("real", &thawing, jan);
    assert(thaw.roadSnow < 0.3);   // a thaw clears the roads first
    // the climate (a moment beyond the forecast) is no real report: a typical winter
    assert(seasonFor("real", climateWeather(jan).get(), jan).snow == 1);
    // snow falling in July settles; on the road as well when it freezes
    const SeasonState july = seasonFor("real", FIXED_WEATHER("snow").get(), localDay(2027, 6, 15));
    assert(july.snow >= 0.75 && july.roadSnow >= 0.5);
    assert(july.leaves == 1);   // the trees keep their leaves
    assert(seasonFor("summer", FIXED_WEATHER("rain").get()).snow == 0);
    // without a date: the game's clock
    assert(seasonFor("real").season == seasonOf(plat::dateMs()));
  }

  // the materials: taught once, a uniform per season – the trees compiled anew only when their crowns get thin
  {
    SeasonLook look;
    TexturePtr tex = dataTexture(std::vector<uint8_t>(4), 1, 1);
    MaterialPtr terrain = makeTerrainMaterial(tex);
    assert(terrain->patch->key == "fs-terrain");
    MaterialPtr tree = lambert({.vertexColors = true});
    look.patch(*terrain, "terrain");
    look.patch(*terrain, "terrain");
    look.patch(*tree, "deciduous");
    assert(terrain->patch->key == "fs-terrain|fs-season-terrain");
    assert(endsWith(tree->patch->key, "|fs-season-deciduous"));
    const ShaderPatch& t = *terrain->patch;
    assert(contains(t.fragmentSource, "fsSeasonGround(diffuseColor.rgb)") && contains(t.fragmentSource, "fsSnowCover("));
    assert(contains(t.vertexSource, "vFsSeasonXZ ="));
    assert(t.uniforms.at("uFsSnow") == look.uniforms.uFsSnow);   // shared uniforms
    const ShaderPatch& leafy = *tree->patch;
    assert(contains(leafy.vertexSource, "fsCrown") && !contains(leafy.fragmentSource, "#define FS_THIN"));
    // winter: uniforms, and the trees once more
    const uint32_t v = tree->version;
    const uint32_t vt = terrain->version;
    look.set(SEASON_PRESETS("winter"));
    assert(std::get<double>(look.uniforms.uFsSnow->value) == 1);
    assert(std::get<double>(look.uniforms.uFsLeaves->value) == 0);
    assert(look.thinCrowns());
    assert(tree->version > v && terrain->version == vt);
    assert(endsWith(tree->patch->key, "-thin"));
    assert(contains(tree->patch->fragmentSource, "#define FS_THIN"));
    assert(tree->patch->uniforms.at("uFsLeaves") == look.uniforms.uFsLeaves);
    look.set(SEASON_PRESETS("autumn"));
    const uint32_t v2 = tree->version;
    look.set(SEASON_PRESETS("winter"));
    assert(tree->version == v2);   // thin already: nothing to compile
    look.set(SEASON_PRESETS("summer"));
    assert(!look.thinCrowns() && !endsWith(tree->patch->key, "-thin"));
    // every material the game teaches the season has its shader (render/glsl_gen.cpp)
    for (MaterialPtr m : {makeGroundMaterial(false), makeGroundMaterial(true), makeBuildingMaterial(false), makeBuildingMaterial(true), makeStructMaterial()}) {
      const std::string own = m->patch ? m->patch->key : "";
      const std::string kind = own == "fs-ground" ? "ground" : own == "fs-paint" ? "paint" : own.empty() ? "struct" : "building";
      look.patch(*m, kind);
      assert(m->patch && m->patch->key == own + "|fs-season-" + kind);
    }
    for (const char* kind : {"orchard", "evergreen"}) {
      MaterialPtr m = lambert({.vertexColors = true});
      look.patch(*m, kind);
      assert(m->patch && m->patch->key == std::string("|fs-season-") + kind);
    }
  }

  // wheel tracks in the snow: two soft strips along each lane near the camera, only while snow lies on the road
  {
    // a lane 400 m long running east at 200 m above the sea, its middle 1.75 m south of the road's
    Edge edge;
    edge.pl = makePolyline({0, 200, 0, 400, 200, 0});
    edge.flags = 0;
    edge.minX = 0;
    edge.maxX = 400;
    edge.minZ = -5;
    edge.maxZ = 5;
    Lane lane;
    lane.s0 = 0;
    lane.s1 = 400;
    lane.dir = 1;
    lane.len = 400;
    lane.off = -1.75;
    lane.edge = &edge;
    Edge tunnelEdge;
    tunnelEdge.pl = edge.pl;
    tunnelEdge.flags = 2;
    tunnelEdge.minX = 0;
    tunnelEdge.maxX = 400;
    tunnelEdge.minZ = -5;
    tunnelEdge.maxZ = 5;
    Lane tunnel = lane;
    tunnel.edge = &tunnelEdge;
    Edge farEdge;
    farEdge.pl = edge.pl;
    farEdge.minX = 5000;
    farEdge.maxX = 5400;
    farEdge.minZ = -5;
    farEdge.maxZ = 5;
    Lane elsewhere = lane;
    elsewhere.edge = &farEdge;
    SnowTracks tracks;
    tracks.update(100, 0, std::vector<Lane*>{&lane});
    assert(tracks.mesh->visible == false);
    assert(tracks.mesh->geometry->index == nullptr);   // nothing built without snow on the road
    tracks.amount = SEASON_PRESETS("winter").roadSnow;
    // (the camera above the lane's middle line)
    tracks.update(100, 1.75, std::vector<Lane*>{&lane, &tunnel, &elsewhere, nullptr});
    assert(tracks.mesh->visible);
    assert(std::abs(tracks.mesh->material->opacity - 0.63) < 1e-9);
    const GeometryPtr g = tracks.mesh->geometry;
    const AttrPtr p = g->position();
    // within 184 m of the camera: x = 0 … 284, a point every 4 m – six vertices each (two tracks, three across)
    assert(p->count() == 72 * 6);   // not in the tunnel, not far away
    assert(g->index->count() == 71 * 2 * 2 * 6);
    std::set<std::string> offsets;
    for (size_t i = 0; i < p->count(); i++) {
      assert(std::abs(p->getY(i) - 200.03) < 1e-4);
      assert(p->getX(i) >= 0 && p->getX(i) <= 284.001);
      offsets.insert(toFixed(std::abs(p->getZ(i) - 1.75), 2));
    }
    assert((offsets == std::set<std::string>{toFixed(HALF_TRACK - 0.26, 2), toFixed(HALF_TRACK, 2), toFixed(HALF_TRACK + 0.26, 2)}));
    const AttrPtr alpha = g->color();
    assert(alpha->itemSize == 4);
    for (size_t i = 0; i < 6; i++) assert(alpha->getW(i) == (i == 1 || i == 4 ? 1 : 0));   // soft edges
    // a few metres on: nothing rebuilt; far on: laid anew
    tracks.update(120, 0, std::vector<Lane*>{&lane});
    assert(tracks.mesh->geometry == g);
    tracks.update(250, 0, std::vector<Lane*>{&lane});
    assert(tracks.mesh->geometry != g);
    tracks.amount = 0;
    tracks.update(250, 0, std::vector<Lane*>{&lane});
    assert(tracks.mesh->visible == false);
    tracks.dispose();
  }

  std::printf("game_season_test: ok\n");
  return 0;
}
