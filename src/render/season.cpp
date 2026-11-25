// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/season.ts @ v1.7.7
#include "render/season.h"

#include <algorithm>
#include <cmath>

#include "platform/plat.h"
#include "render/glsl_gen.h"
#include "world/cover.h"

namespace fs {

const std::vector<Season> SEASONS = {"spring", "summer", "autumn", "winter"};
const std::vector<SeasonSetting> SEASON_SETTINGS = {"real", "spring", "summer", "autumn", "winter"};

bool isSeasonSetting(const std::string& v) { return std::find(SEASON_SETTINGS.begin(), SEASON_SETTINGS.end(), v) != SEASON_SETTINGS.end(); }

namespace {

/** of the snow beside the road, how much lies on it (the rest is cleared, driven away, salted) */
constexpr double ROAD_SHARE = 0.7;

double clamp01(double v) { return v <= 0 ? 0 : v >= 1 ? 1 : v; }

double smooth(double e0, double e1, double x) {
  const double t = clamp01((x - e0) / (e1 - e0));
  return t * t * (3 - 2 * t);
}

double mix(double a, double b, double t) { return a + (b - a) * t; }

// ------------------------------------------------------------ the dates

constexpr int MONTH_START[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};

}  // namespace

int dayOfYear(double date) {
  const DateFields f = plat::localFields(date);
  return MONTH_START[f.month] + f.date;
}

SeasonState seasonAt(double date, bool typicalSnow) {
  const double d = dayOfYear(date);
  // the leaves come out from mid-April, are full by mid-May; turn from late September; fall from mid-October to late November
  const double leaves = smooth(100, 135, d) * (1 - smooth(290, 325, d));
  const double spring = smooth(95, 115, d) * (1 - smooth(140, 175, d));
  // fruit trees: the second half of April
  const double blossom = smooth(98, 108, d) * (1 - smooth(122, 132, d));
  const double autumn = d > 182 ? smooth(262, 300, d) : 0;
  const double dormant = d > 182 ? smooth(300, 340, d) : 1 - smooth(75, 110, d);
  const double snow = typicalSnow ? (d > 182 ? smooth(345, 360, d) : 1 - smooth(45, 60, d)) : 0;
  return {.season = seasonOf(date), .leaves = leaves, .spring = spring, .blossom = blossom, .autumn = autumn, .dormant = dormant, .snow = snow, .roadSnow = snow * ROAD_SHARE};
}

const SeasonState& SEASON_PRESETS(const std::string& season) {
  static const auto* t = new std::unordered_map<std::string, SeasonState>{
      {"spring", {.season = "spring", .leaves = 0.9, .spring = 1, .blossom = 1, .autumn = 0, .dormant = 0, .snow = 0, .roadSnow = 0}},
      {"summer", {.season = "summer", .leaves = 1, .spring = 0, .blossom = 0, .autumn = 0, .dormant = 0, .snow = 0, .roadSnow = 0}},
      {"autumn", {.season = "autumn", .leaves = 0.75, .spring = 0, .blossom = 0, .autumn = 0.9, .dormant = 0.25, .snow = 0, .roadSnow = 0}},
      {"winter", {.season = "winter", .leaves = 0, .spring = 0, .blossom = 0, .autumn = 1, .dormant = 1, .snow = 1, .roadSnow = ROAD_SHARE}},
  };
  auto it = t->find(season);
  return it != t->end() ? it->second : t->at("summer");
}

SeasonState seasonFor(const SeasonSetting& setting, const WeatherState* weather, std::optional<double> dateIn) {
  const double date = dateIn ? *dateIn : gameNow();
  const bool real = weather && weather->source == "real";
  SeasonState s = setting == "real" ? seasonAt(date, !real) : SEASON_PRESETS(setting);
  if (weather) {
    const double lying = snowOnGround(*weather);
    // the real weather says how much lies; otherwise what falls (or a chosen snowy weather) adds to the season's
    s.snow = setting == "real" && real ? lying : std::max(s.snow, lying);
  }
  s.roadSnow = s.snow * ROAD_SHARE;
  if (weather && weather->kind == "snow" && weather->intensity > 0 && weather->temperature <= 0) s.roadSnow = std::max(s.roadSnow, 0.3 + 0.4 * clamp01(weather->intensity));
  // a thaw: the roads clear first
  if (setting == "real" && weather && real && weather->temperature > 3) s.roadSnow *= 0.4;
  return s;
}

// ------------------------------------------------- the colours, as numbers

namespace {

/** sRGB hex → linear RGB (as the shaders see colours) */
RGB lin(uint32_t hex) {
  const Color c(hex);
  return {c.r, c.g, c.b};
}

double lum(const RGB& c) { return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]; }

/** the green the broadleaf crowns are drawn in (props.ts) – the season's crown colours are factors on it */
const RGB& CROWN_GREEN() {
  static const RGB c = lin(0x4c7a36);
  return c;
}
RGB tint(uint32_t hex) {
  const RGB c = lin(hex);
  const RGB& g = CROWN_GREEN();
  return {c[0] / g[0], c[1] / g[1], c[2] / g[2]};
}

/** the crowns: the young leaves' green, the autumn colours (yellow, orange, red, brown), blossom, bare twigs */
struct Crowns {
  RGB spring = tint(0x64953a);
  std::array<RGB, 4> autumn = {tint(0xc9a227), tint(0xc8641e), tint(0xa5321e), tint(0x7a5a2e)};
  RGB blossom = tint(0xf0dce4);
  RGB twigs = tint(BARE_CROWN);
};
const Crowns& crowns() {
  static const auto* c = new Crowns();
  return *c;
}
/** how much of a bare crown is drawn: all of it – a soft grey-brown mass of twigs (drawn see-through,
 *  against a bright sky a crown only showed as speckles) */
constexpr double TWIG_COVER = 1;

/** the ground: factors on green plants in spring, in autumn; the leaf litter on the forest floor */
constexpr RGB VEG_SPRING = {1.1, 1.15, 0.9};
constexpr RGB VEG_AUTUMN = {1.15, 0.92, 0.8};
constexpr RGB LITTER = {2.2, 0.85, 0.7};
/** dormant grass: its own brightness, olive and dull */
constexpr RGB VEG_DORMANT = {1.1, 1.0, 0.78};
/** the fields: young crops, ploughed in autumn, bare in winter – at the brightness of the farmland colour */
struct Fields {
  RGB spring = lin(0x7c9a4a);
  RGB autumn = lin(0x86704f);
  RGB dormant = lin(0x76654d);
  double lum = fs::lum(lin(0xa9a35c));
};
const Fields& fieldColours() {
  static const auto* f = new Fields();
  return *f;
}

}  // namespace

RGB seasonGround(const RGB& c, const SeasonState& s) {
  const Fields& F = fieldColours();
  const double mx = std::max({c[0], c[1], c[2], 1e-4});
  const double veg = smooth(0.08, 0.3, (c[1] - std::max(c[0], c[2])) / mx);
  const double field = smooth(0.3, 0.45, (std::min(c[0], c[1]) - c[2]) / mx) * (1 - smooth(0.08, 0.2, std::abs(c[0] - c[1]) / mx));
  const double l = lum(c);
  const double dark = 1 - smooth(0.06, 0.14, l);
  const double fl = l / F.lum;
  const double litter = std::max(s.autumn * (1 - s.leaves), s.dormant);
  RGB o = c;
  for (size_t i = 0; i < 3; i++) {
    double v = c[i];
    v = mix(v, v * VEG_SPRING[i], s.spring * veg);
    v = mix(v, F.spring[i] * fl, s.spring * field * 0.7);
    v = mix(v, v * VEG_AUTUMN[i], s.autumn * veg * (1 - dark) * 0.6);
    v = mix(v, v * LITTER[i], veg * dark * litter * 0.8);
    v = mix(v, F.autumn[i] * fl, s.autumn * field * 0.7);
    o[i] = v;
  }
  const double dl = 0.2126 * o[0] + 0.7152 * o[1] + 0.0722 * o[2];
  RGB out;
  for (size_t i = 0; i < 3; i++) {
    double v = o[i];
    v = mix(v, dl * VEG_DORMANT[i], s.dormant * veg * (1 - dark) * 0.55);
    v = mix(v, F.dormant[i] * fl, s.dormant * field * 0.75);
    out[i] = v;
  }
  return out;
}

uint32_t coverColor(int cls, const SeasonState& s) {
  const uint32_t base = cls >= 0 && cls < 18 ? COVER_COLORS[cls] : COVER_COLORS[0];
  const RGB c = seasonGround(lin(base), s);
  Color col;
  col.setRGB(c[0], c[1], c[2]);
  return static_cast<uint32_t>(col.getHex());
}

std::vector<uint32_t> coverPalette(const SeasonState& s) {
  std::vector<uint32_t> out;
  for (int i = 0; i < 18; i++) out.push_back(coverColor(i, s));
  return out;
}

CrownLook crownLook(const SeasonState& s, const TreeKind& kind, double r1, double r2, bool blossomHere) {
  if (kind == "evergreen") return {{1, 1, 1}, 1};
  const Crowns& C = crowns();
  const double turn = clamp01(s.autumn * 1.4 - r2 * 0.4);
  const RGB& aut = C.autumn[r2 < 0.45 ? 0 : r2 < 0.7 ? 1 : r2 < 0.85 ? 2 : 3];
  const bool blooms = kind == "orchard" || r1 > 0.85;
  const double b = blooms && blossomHere ? s.blossom : 0;
  const double has = clamp01(s.leaves * 1.3 - r1 * 0.3);
  RGB t = {1, 1, 1};
  for (size_t i = 0; i < 3; i++) {
    double v = mix(1, C.spring[i], s.spring);
    v = mix(v, aut[i], turn);
    v = mix(v, C.blossom[i], b);
    t[i] = mix(C.twigs[i], v, has);
  }
  return {t, TWIG_COVER + (1 - TWIG_COVER) * has};
}

double snowCover(double amount, double up, double lo, double hi, double noise) {
  if (amount <= 0.001) return 0;
  const double e = 1.15 - 1.3 * amount;
  return smooth(lo, hi, up) * smooth(e - 0.15, e + 0.15, noise);
}

// ------------------------------------------------------- the season look

namespace {

/** below this much foliage the crowns get thin (and the tree materials their see-through variant) */
constexpr double THIN_BELOW = 0.97;

bool isTree(const SeasonTarget& t) { return t == "deciduous" || t == "orchard" || t == "evergreen"; }

}  // namespace

void SeasonLook::set(const SeasonState& s) {
  state = s;
  Uniforms& u = uniforms;
  u.uFsLeaves->value = clamp01(s.leaves);
  u.uFsSpring->value = clamp01(s.spring);
  u.uFsBlossom->value = clamp01(s.blossom);
  u.uFsAutumn->value = clamp01(s.autumn);
  u.uFsDormant->value = clamp01(s.dormant);
  u.uFsSnow->value = clamp01(s.snow);
  u.uFsRoadSnow->value = clamp01(s.roadSnow);
  const bool thin = s.leaves < THIN_BELOW;
  if (thin != thin_) {
    thin_ = thin;
    // (the original's onBeforeCompile asks whether the crowns are thin as the program is made: here the shader for it is put on now)
    for (Material* m : trees_) {
      apply(*m, patched_.at(m));
      m->needsUpdate();
    }
  }
}

void SeasonLook::apply(Material& material, const Patched& p) {
  const bool tree = isTree(p.target);
  // the program's key: the material's own one and ours – the name of the shader the original's patch leaves
  const std::string name = p.ownKey + "|fs-season-" + p.target + (tree && thin_ && p.target != "evergreen" ? "-thin" : "");
  ShaderPatchPtr next = gamePatch(name.c_str());
  if (!next) {
    plat::warn("season: no shader " + name);
    return;
  }
  next->uniforms = p.ownUniforms;
  const Uniforms& u = uniforms;
  next->uniforms["uFsLeaves"] = u.uFsLeaves;
  next->uniforms["uFsSpring"] = u.uFsSpring;
  next->uniforms["uFsBlossom"] = u.uFsBlossom;
  next->uniforms["uFsAutumn"] = u.uFsAutumn;
  next->uniforms["uFsDormant"] = u.uFsDormant;
  next->uniforms["uFsSnow"] = u.uFsSnow;
  next->uniforms["uFsRoadSnow"] = u.uFsRoadSnow;
  material.patch = next;
}

void SeasonLook::patch(Material& material, const SeasonTarget& target) {
  // (once: a material that has the season's shader already keeps it – told by its program key, not by its
  //  address: a tile's terrain material that is gone may leave its address to the next one)
  if (material.patch && material.patch->key.find("|fs-season-") != std::string::npos) return;
  Patched p;
  p.ownKey = material.patch ? material.patch->key : "";
  p.target = target;
  if (material.patch) p.ownUniforms = material.patch->uniforms;
  if (isTree(target)) {
    // (the trees are patched anew when their crowns get thin: what they had is kept for that)
    const Patched& kept = patched_.insert_or_assign(&material, std::move(p)).first->second;
    if (std::find(trees_.begin(), trees_.end(), &material) == trees_.end()) trees_.push_back(&material);
    apply(material, kept);
  } else apply(material, p);
  material.needsUpdate();
}

}  // namespace fs
