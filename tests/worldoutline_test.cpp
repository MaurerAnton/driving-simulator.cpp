// LINK: ui geo core platform
// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The outline of the countries (web/data/outline.bin, read by ui/worldoutline): known
 * places lie in their countries, the file has a sensible size and shape, and no
 * broken file brings the reader down.
 */
#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

#include "geo/utm.h"
#include "ui/worldoutline.h"

using namespace fs;

namespace {

struct Place {
  const char* name;
  double lat, lon;
  /** where the script that wrote the file puts it (`node tools/build-outline.mjs --points`) */
  double x, z;
  /** the country it lies in ("": in the sea) */
  const char* iso;
};
const Place PLACES[] = {
    {"Kassel", 51.3127, 9.4797, 5433.5121, -6708.4928, "DE"},
    {"Berlin", 52.52, 13.405, 270812.8026, -149999.8999, "DE"},
    {"Vienna", 48.2082, 16.3738, 519664.4983, 312214.8118, "AT"},
    {"Zürich", 47.3769, 8.5417, -62596.7155, 430849.1607, "CH"},
    {"Brussels", 50.8503, 4.3517, -355145.0138, 34520.7762, "BE"},
    {"Paris", 48.8566, 2.3522, -515483.4173, 245143.2731, "FR"},
    {"Nice", 43.7102, 7.262, -168020.0534, 836846.0753, "FR"},
    {"Rome", 41.9028, 12.4964, 262023.4812, 1033101.7195, "IT"},
    {"Oslo", 59.9139, 10.7522, 69979.9029, -965118.9914, "NO"},
    {"Bolzano", 46.4983, 11.3548, 152691.2859, 525891.4702, "IT"},
    {"Innsbruck", 47.2692, 11.4041, 153848.8821, 440116.7587, "AT"},
    {"Strasbourg", 48.5734, 7.7521, -120050.6206, 297213.8713, "FR"},
    {"San Marino", 43.9424, 12.4578, 249500.5304, 806710.2976, "SM"},   // (a hole in Italy)
    {"Luxembourg", 49.6116, 6.1319, -235185.7176, 178600.6704, "LU"},
    {"the North Sea", 56.0, 3.0, -401950.0293, -544336.3352, ""},
    {"the Adriatic", 43.5, 14.5, 416679.8955, 846943.6579, ""},
};

Bytes fileBytes(const char* path) {
  std::ifstream f(path, std::ios::binary);
  return Bytes(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

/** twice the area inside a ring: positive for a ring, negative for a hole */
double area2(const std::vector<double>& xz) {
  double a = 0;
  for (size_t i = 0, n = xz.size(); i < n; i += 2) {
    const size_t j = (i + 2) % n;
    a += xz[i] * xz[j + 1] - xz[j] * xz[i + 1];
  }
  return a;
}

/** are the points of `some` points of `all`, in the same order? */
bool pointsOf(const std::vector<double>& some, const std::vector<double>& all) {
  size_t k = 0;
  for (size_t i = 0; i < some.size(); i += 2) {
    while (k < all.size() && (all[k] != some[i] || all[k + 1] != some[i + 1])) k += 2;
    if (k >= all.size()) return false;
    k += 2;
  }
  return true;
}

bool within(const std::array<double, 4>& inner, const std::array<double, 4>& outer) {
  return inner[0] >= outer[0] && inner[1] >= outer[1] && inner[2] <= outer[2] && inner[3] <= outer[3];
}

/** does all of a line run along the frame's edge (where the map merely ends)? */
bool alongFrame(const std::vector<double>& xz, const std::array<double, 4>& frame) {
  for (size_t i = 0; i + 3 < xz.size(); i += 2) {
    const bool alongX = xz[i] == xz[i + 2] && (xz[i] == frame[0] || xz[i] == frame[2]);
    const bool alongZ = xz[i + 1] == xz[i + 3] && (xz[i + 1] == frame[1] || xz[i + 1] == frame[3]);
    if (!alongX && !alongZ) return false;
  }
  return true;
}

struct Counts {
  int rings = 0, farRings = 0, holes = 0;
  size_t points = 0, farPoints = 0;
};

/** What every ring must be like, whatever the file says; counted on the way. */
void checkRings(const std::vector<OutlineRing>& rings, const std::array<double, 4>& box, const WorldOutline& o, Counts& n) {
  assert(!rings.empty());
  assert(within(box, o.box));
  for (const OutlineRing& r : rings) {
    assert(r.xz.size() >= 6 && r.xz.size() % 2 == 0);
    assert(within(r.box, box));
    for (size_t i = 0; i < r.xz.size(); i += 2) assert(r.xz[i] >= r.box[0] && r.xz[i] <= r.box[2] && r.xz[i + 1] >= r.box[1] && r.xz[i + 1] <= r.box[3]);
    // a ring runs one way round, a hole the other: filled non-zero, a path of both leaves the hole out
    assert((area2(r.xz) > 0) == !r.hole);
    n.rings++;
    n.holes += r.hole ? 1 : 0;
    n.points += r.xz.size() / 2;
    if (r.far.empty()) continue;
    // the far form: fewer of the same points, the same way round
    assert(r.far.size() >= 6 && r.far.size() <= r.xz.size());
    assert(pointsOf(r.far, r.xz));
    assert((area2(r.far) > 0) == !r.hole);
    n.farRings++;
    n.farPoints += r.far.size() / 2;
  }
}

const OutlineCountry* byCode(const WorldOutline& o, const std::string& iso) {
  for (const OutlineCountry& c : o.countries)
    if (c.iso == iso) return &c;
  return nullptr;
}

/** how often the rings of a lake go round a point (0: not in its water) */
int lakeWinding(const OutlineLake& lake, double x, double z) {
  int w = 0;
  for (const OutlineRing& r : lake.rings) {
    for (size_t i = 0, n = r.xz.size(); i < n; i += 2) {
      const size_t j = (i + 2) % n;
      const double side = (r.xz[j] - r.xz[i]) * (z - r.xz[i + 1]) - (x - r.xz[i]) * (r.xz[j + 1] - r.xz[i + 1]);
      if (r.xz[i + 1] <= z && r.xz[j + 1] > z && side > 0) w++;
      if (r.xz[i + 1] > z && r.xz[j + 1] <= z && side < 0) w--;
    }
  }
  return w;
}

bool inALake(const WorldOutline& o, double lat, double lon) {
  const auto p = latLonToWorld(lat, lon);
  for (const OutlineLake& lake : o.lakes)
    if (lakeWinding(lake, p.first, p.second) != 0) return true;
  return false;
}

/**
 * A file made by hand, byte for byte as tools/build-outline.mjs describes it: one
 * country "XY", a triangle of 500 m, whose one ring walks its one arc `walks` times.
 */
Bytes handMade(int walks) {
  Bytes b = {'F', 'S', 'O', 'L', 1};
  const auto v = [&b](uint32_t n) {   // a varint
    for (; n >= 128; n >>= 7) b.push_back(static_cast<uint8_t>((n & 127) | 128));
    b.push_back(static_cast<uint8_t>(n));
  };
  const auto s = [&v](int n) { v(static_cast<uint32_t>(n < 0 ? -2 * n - 1 : 2 * n)); };   // a signed one
  for (uint32_t metres : {50u, 300u, 2000u}) v(metres);   // unit, tolerance, farTolerance
  for (int step : {-100, -100, 100, 100}) s(step);        // the frame: 5 km around the origin
  v(1);                                                   // one arc:
  v(0 + 4);                                               //   a coast, in the far level too
  v(4);                                                   //   four points: (0,0) (10,0) (0,10) and, the line being closed, (0,0) again
  for (int step : {0, 0, 10, 0, -10, 10, 0, -10}) s(step);
  b.push_back(0x0b);                                      //   in the far level: the first, the second and the last
  v(1);                                                   // one country:
  for (char c : {'X', 'Y', '\2', 'X', 'y', '\3', 'Y', 'x', '!'}) b.push_back(static_cast<uint8_t>(c));   // its code and two names
  s(3);                                                   //   its label at (3, 3)
  s(3);
  v(1);                                                   //   one ring:
  v(0);                                                   //     no hole, not in the far level
  v(static_cast<uint32_t>(walks));                        //     of so many arcs,
  for (int i = 0; i < walks; i++) v(0);                   //     arc 0 forwards each time
  v(0);                                                   // no lakes
  return b;
}

}  // namespace

int main() {
  const Bytes bytes = fileBytes("web/data/outline.bin");
  // fetched once and kept: well under 400 kB
  assert(bytes.size() > 20000 && bytes.size() < 200000);

  const WorldOutline o = parseWorldOutline(bytes);
  assert(!o.empty());
  // the frame holds every region (x −470 … +540 km, z −1400 … +940 km) with room to spare
  assert(o.box[0] <= -600000 && o.box[1] <= -1500000 && o.box[2] >= 650000 && o.box[3] >= 1050000);
  assert(o.tolerance >= 50 && o.tolerance <= 500);
  assert(o.farTolerance > o.tolerance && o.farTolerance <= 5000);

  // --- the projection: the script that wrote the file and geo/utm agree to the millimetre ---
  for (const Place& p : PLACES) {
    const auto w = latLonToWorld(p.lat, p.lon);
    assert(std::fabs(w.first - p.x) < 1e-3 && std::fabs(w.second - p.z) < 1e-3);
  }

  // --- the countries ---
  assert(o.countries.size() >= 40 && o.countries.size() <= 70);
  Counts n;
  for (size_t i = 0; i < o.countries.size(); i++) {
    const OutlineCountry& c = o.countries[i];
    assert(c.iso.size() == 2 && !c.nameEn.empty() && !c.nameDe.empty());
    if (i > 0) assert(o.countries[i - 1].iso < c.iso);   // (in the order of their codes, each once)
    checkRings(c.rings, c.box, o, n);
    // its name can stand at its label: that point lies in it
    assert(countryAt(o, c.labelX, c.labelZ) == &c);
  }
  const char* const NAMES[][3] = {{"DE", "Germany", "Deutschland"}, {"AT", "Austria", "Österreich"}, {"CH", "Switzerland", "Schweiz"},
                                  {"BE", "Belgium", "Belgien"},     {"FR", "France", "Frankreich"},  {"IT", "Italy", "Italien"},
                                  {"NO", "Norway", "Norwegen"}};
  for (const auto& name : NAMES) {
    const OutlineCountry* c = byCode(o, name[0]);
    assert(c && c->nameEn == name[1] && c->nameDe == name[2]);
  }
  // Germany as a whole lies in the frame: 357 600 km² (the projection stretches areas by a few thousandths)
  double germany = 0;
  for (const OutlineRing& r : byCode(o, "DE")->rings) germany += area2(r.xz) / 2;
  assert(germany > 354e9 && germany < 361e9);
  const int landRings = n.rings;

  // --- the lakes ---
  assert(o.lakes.size() >= 50 && o.lakes.size() <= 700);
  for (const OutlineLake& lake : o.lakes) checkRings(lake.rings, lake.box, o, n);
  assert(inALake(o, 47.62, 9.38));    // Lake Constance, off Friedrichshafen
  assert(inALake(o, 46.45, 6.55));    // Lake Geneva, off Lausanne
  assert(inALake(o, 52.8, 5.35));     // the IJsselmeer
  assert(!inALake(o, 52.51, 5.47));   // Lelystad, on the polder in it: a hole of the lake
  assert(!inALake(o, 51.3127, 9.4797));

  // --- how much there is: light enough to draw, and the far form much lighter ---
  assert(landRings >= 150 && landRings <= 1500);
  assert(n.rings >= 250 && n.rings <= 2500);
  assert(n.holes >= 2);   // (San Marino and the Vatican in Italy, at least)
  assert(n.points >= 10000 && n.points <= 120000);
  assert(n.farRings >= 50 && n.farRings < n.rings);
  assert(n.farPoints >= 1500 && n.farPoints * 2 <= n.points);

  // --- the lines: every coast, border and shore once; the frame's edge is none of them ---
  assert(o.lines.size() >= 200 && o.lines.size() <= 3000);
  int kinds[3] = {0, 0, 0};
  size_t linePoints = 0;
  for (const OutlineLine& l : o.lines) {
    assert(l.kind == OutlineLine::Coast || l.kind == OutlineLine::Border || l.kind == OutlineLine::Shore);
    kinds[l.kind]++;
    assert(l.xz.size() >= 4 && l.xz.size() % 2 == 0);
    assert(within(l.box, o.box));
    linePoints += l.xz.size() / 2;
    assert(l.far.empty() || (l.far.size() >= 4 && pointsOf(l.far, l.xz)));
    assert(!alongFrame(l.xz, o.box) && (l.far.empty() || !alongFrame(l.far, o.box)));
  }
  assert(kinds[OutlineLine::Coast] >= 50 && kinds[OutlineLine::Border] >= 30 && kinds[OutlineLine::Shore] >= 50);
  // (a border is in the rings of both its countries, but one line: fewer points than the rings have)
  assert(linePoints < n.points);

  // --- which country a place lies in ---
  for (const Place& p : PLACES) {
    const auto w = latLonToWorld(p.lat, p.lon);
    const OutlineCountry* c = countryAt(o, w.first, w.second);
    if (std::string(p.iso).empty()) assert(c == nullptr);
    else assert(c && c->iso == p.iso);
  }
  assert(countryAt(o, 5e6, 0) == nullptr && countryAt(o, 0, -5e6) == nullptr);   // beyond the frame

  // --- which country a region belongs to (the regions' boxes as regions.json has them) ---
  // Kassel's surroundings: all of it Germany
  assert(countryOfBox(o, {-2000, -14000, 13000, 1000}) == byCode(o, "DE"));
  // Basel: Germany and France begin in the box; Bouillon: most of the box is France – the middle decides
  assert(countryOfBox(o, {-141000, 404000, -126000, 417000}) == byCode(o, "CH"));
  assert(countryOfBox(o, {-331000, 147000, -316000, 162000}) == byCode(o, "BE"));
  // Ålesund: a town on islands between fjords, the middle of its box is water on this map – the land around decides
  assert(countryAt(o, -174500, -1252500) == nullptr);
  assert(countryOfBox(o, {-182000, -1260000, -167000, -1245000}) == byCode(o, "NO"));
  assert(countryOfBox(o, {-238000, -1031000, -223000, -1016000}) == byCode(o, "NO"));   // Bergen
  // out at sea
  assert(countryOfBox(o, {-410000, -552000, -395000, -537000}) == nullptr);

  // --- the file as it is described, made by hand ---
  {
    const WorldOutline tiny = parseWorldOutline(handMade(1));
    assert(tiny.countries.size() == 1 && tiny.lakes.empty() && tiny.lines.size() == 1);
    assert(tiny.box == (std::array<double, 4>{-5000, -5000, 5000, 5000}) && tiny.tolerance == 300 && tiny.farTolerance == 2000);
    const OutlineCountry& c = tiny.countries[0];
    assert(c.iso == "XY" && c.nameEn == "Xy" && c.nameDe == "Yx!" && c.labelX == 150 && c.labelZ == 150);
    assert(c.rings.size() == 1 && !c.rings[0].hole && c.rings[0].far.empty());
    assert(c.rings[0].xz == (std::vector<double>{0, 0, 500, 0, 0, 500}));
    assert(c.box == (std::array<double, 4>{0, 0, 500, 500}) && c.rings[0].box == c.box);
    // its coast as a line: closed, so the first point comes again; from far, two of the three points
    const OutlineLine& l = tiny.lines[0];
    assert(l.kind == OutlineLine::Coast && l.xz == (std::vector<double>{0, 0, 500, 0, 0, 500, 0, 0}) && l.far == (std::vector<double>{0, 0, 500, 0, 0, 0}));
    assert(countryAt(tiny, 100, 100) == &c && countryAt(tiny, 400, 400) == nullptr && countryAt(tiny, -100, 100) == nullptr);
    // a box whose middle (−100, −100) is not in the triangle, but one of the 25 points spread over it (220, 220) is
    assert(countryOfBox(tiny, {-900, -900, 700, 700}) == &c && countryOfBox(tiny, {-900, -900, -100, -100}) == nullptr);
    // a ring may walk a line as often as a line has sides to share, not over and over
    assert(parseWorldOutline(handMade(4)).countries.size() == 1 && parseWorldOutline(handMade(5)).empty());
  }

  // --- files that are not what they should be: an empty outline, never a crash ---
  assert(parseWorldOutline(Bytes{}).empty());
  const std::string text = "{\"latest\": \"hesse\", \"regions\": []}";
  assert(parseWorldOutline(Bytes(text.begin(), text.end())).empty());
  assert(parseWorldOutline(Bytes(5000, 0)).empty() && parseWorldOutline(Bytes(5000, 0xff)).empty());
  {
    Bytes b = bytes;
    b[0] = 'G';   // not the magic word
    assert(parseWorldOutline(b).empty());
    b = bytes;
    b[4] = 2;   // a version of the future
    assert(parseWorldOutline(b).empty());
    b = bytes;
    b.push_back(0);   // more than the file should have
    assert(parseWorldOutline(b).empty());
  }
  // cut off anywhere (a download that broke off): every length at the two ends, one in 97 between
  int cuts = 0;
  for (size_t len = 0; len < bytes.size(); len += (len < 300 || len + 100 > bytes.size()) ? 1 : 97) {
    assert(parseWorldOutline(Bytes(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(len))).empty());
    cuts++;
  }
  // one bit wrong anywhere: what comes out is empty, or (a letter of a name, a point moved
  // within the frame) still an outline as well formed as any
  uint32_t seed = 20261124;
  int survived = 0;
  const int DAMAGED = 200;
  for (int i = 0; i < DAMAGED; i++) {
    seed = seed * 1664525u + 1013904223u;
    Bytes b = bytes;
    const size_t at = (seed >> 8) % b.size();
    seed = seed * 1664525u + 1013904223u;
    b[at] ^= static_cast<uint8_t>(1u << ((seed >> 8) % 8));
    const WorldOutline broken = parseWorldOutline(b);
    if (broken.empty()) continue;
    survived++;
    for (const OutlineCountry& c : broken.countries) {
      assert(!c.rings.empty() && within(c.box, broken.box));
      for (const OutlineRing& r : c.rings) assert(r.xz.size() >= 6 && r.xz.size() % 2 == 0 && r.far.size() % 2 == 0 && within(r.box, c.box));
    }
    for (const OutlineLake& lake : broken.lakes) {
      assert(!lake.rings.empty() && within(lake.box, broken.box));
      for (const OutlineRing& r : lake.rings) assert(r.xz.size() >= 6 && r.xz.size() % 2 == 0 && r.far.size() % 2 == 0 && within(r.box, lake.box));
    }
    for (const OutlineLine& l : broken.lines) assert(l.xz.size() >= 4 && l.xz.size() % 2 == 0 && l.far.size() % 2 == 0 && within(l.box, broken.box));
    // (and looking a place up in it does no harm)
    countryAt(broken, 5433.5, -6708.5);
    countryOfBox(broken, {-182000, -1260000, -167000, -1245000});
  }

  std::printf("worldoutline: all checks passed (%zu bytes: %zu countries and %zu lakes in %d rings of %zu points, far %d rings of %zu; %zu lines; %d cut-off and %d damaged files, %d of those still readable)\n",
              bytes.size(), o.countries.size(), o.lakes.size(), n.rings, n.points, n.farRings, n.farPoints, o.lines.size(), cuts, DAMAGED, survived);
  return 0;
}
