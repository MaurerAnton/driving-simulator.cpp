// SPDX-License-Identifier: AGPL-3.0-only
// (New in 2.0.)  What the file looks like is written down in tools/build-outline.mjs.
#include "ui/worldoutline.h"

#include <algorithm>
#include <cstdint>

namespace fs {

namespace {

/** The file, read one number after the other; `ok` turns false at the first one that is not there. */
struct In {
  const uint8_t* p;
  const uint8_t* end;
  bool ok = true;

  size_t left() const { return static_cast<size_t>(end - p); }
  uint32_t u8() {
    if (p == end) {
      ok = false;
      return 0;
    }
    return *p++;
  }
  /** a varint: seven bits a byte, the low ones first; the top bit says that more follow */
  uint32_t v() {
    uint32_t r = 0;
    for (int shift = 0;; shift += 7) {
      const uint32_t b = u8();
      // (five bytes at most, the fifth with three bits: numbers up to 2^31 − 1)
      if (!ok || shift > 28 || (shift == 28 && b > 7)) {
        ok = false;
        return 0;
      }
      r |= (b & 127) << shift;
      if (b < 128) return r;
    }
  }
  /** a signed number, zigzag-coded: 0, −1, 1, −2 … as 0, 1, 2, 3 … */
  int64_t s() {
    const uint32_t n = v();
    return (n & 1) ? -static_cast<int64_t>(n >> 1) - 1 : static_cast<int64_t>(n >> 1);
  }
  std::string text() {
    const uint32_t n = v();
    if (!ok || n > left()) {
      ok = false;
      return {};
    }
    std::string t(reinterpret_cast<const char*>(p), n);
    p += n;
    return t;
  }
};

/** A stretch of line from one joint to the next, as the file holds it: rings are put together from these. */
struct Arc {
  /** 0 coast, 1 border, 2 shore, 3 a seam that is not drawn (the frame's edge) */
  int kind = 0;
  bool far = false;
  std::vector<double> xz;
  std::vector<double> farXz;
};

std::array<double, 4> boxOf(const std::vector<double>& xz) {
  std::array<double, 4> b{xz[0], xz[1], xz[0], xz[1]};
  for (size_t i = 2; i + 1 < xz.size(); i += 2) {
    b[0] = std::min(b[0], xz[i]);
    b[1] = std::min(b[1], xz[i + 1]);
    b[2] = std::max(b[2], xz[i]);
    b[3] = std::max(b[3], xz[i + 1]);
  }
  return b;
}

bool inBox(const std::array<double, 4>& b, double x, double z) { return x >= b[0] && x <= b[2] && z >= b[1] && z <= b[3]; }

/** An arc's points added to a ring, forwards or backwards – but for the last, which the next arc begins with. */
void walk(std::vector<double>& ring, const std::vector<double>& arc, bool backwards) {
  const size_t n = arc.size() / 2;
  for (size_t i = 0; i + 1 < n; i++) {
    const size_t k = backwards ? n - 1 - i : i;
    ring.push_back(arc[2 * k]);
    ring.push_back(arc[2 * k + 1]);
  }
}

/**
 * The rings of a country or a lake, and the box around them.  False: the file is not as it
 * should be.  (`uses`: how often each arc has been walked so far.  A count is never taken
 * on trust: what it counts must fit into what is left of the file.)
 */
bool readRings(In& in, const std::vector<Arc>& arcs, std::vector<uint8_t>& uses, std::vector<OutlineRing>& rings, std::array<double, 4>& box) {
  const uint32_t count = in.v();
  if (!in.ok || count < 1 || count > in.left() / 3) return false;
  rings.reserve(count);
  for (uint32_t r = 0; r < count; r++) {
    const uint32_t flags = in.v();
    const uint32_t refs = in.v();
    if (!in.ok || flags > 3 || refs < 1 || refs > in.left()) return false;
    OutlineRing ring;
    ring.hole = (flags & 1) != 0;
    const bool far = (flags & 2) != 0;
    double endX = 0;
    double endZ = 0;
    for (uint32_t i = 0; i < refs; i++) {
      const uint32_t ref = in.v();
      if (!in.ok || (ref >> 1) >= arcs.size()) return false;
      const Arc& arc = arcs[ref >> 1];
      // (a line has two sides – two countries, or two lakes: rings that walk it over and over are no map)
      if (++uses[ref >> 1] > 4) return false;
      const bool backwards = (ref & 1) != 0;
      const size_t first = backwards ? arc.xz.size() - 2 : 0;
      const size_t last = backwards ? 0 : arc.xz.size() - 2;
      // an arc begins where the one before it ended
      if (i > 0 && (arc.xz[first] != endX || arc.xz[first + 1] != endZ)) return false;
      endX = arc.xz[last];
      endZ = arc.xz[last + 1];
      walk(ring.xz, arc.xz, backwards);
      if (far) {
        if (!arc.far) return false;
        walk(ring.far, arc.farXz, backwards);
      }
    }
    // … and the last one ends where the first began; a ring has three points at least
    if (ring.xz.size() < 6 || ring.xz[0] != endX || ring.xz[1] != endZ || (far && ring.far.size() < 6)) return false;
    ring.box = boxOf(ring.xz);
    if (r == 0) box = ring.box;
    box = {std::min(box[0], ring.box[0]), std::min(box[1], ring.box[1]), std::max(box[2], ring.box[2]), std::max(box[3], ring.box[3])};
    rings.push_back(std::move(ring));
  }
  return true;
}

/** The file into `o`.  False: it is not an outline, or not all of one (`o` is then half filled). */
bool readOutline(const Bytes& bytes, WorldOutline& o) {
  In in{bytes.data(), bytes.data() + bytes.size()};
  for (const char c : {'F', 'S', 'O', 'L'})
    if (in.u8() != static_cast<uint32_t>(c)) return false;
  if (in.u8() != 1) return false;   // (the version)
  const double unit = in.v();
  o.tolerance = in.v();
  o.farTolerance = in.v();
  int64_t frame[4];
  for (int64_t& f : frame) f = in.s();
  if (!in.ok || unit < 1 || frame[0] >= frame[2] || frame[1] >= frame[3]) return false;
  for (size_t i = 0; i < 4; i++) o.box[i] = static_cast<double>(frame[i]) * unit;

  // the arcs: every point a step from the one before it, from one arc on into the next
  const uint32_t arcCount = in.v();
  if (!in.ok || arcCount > in.left() / 6) return false;
  std::vector<Arc> arcs;
  arcs.reserve(arcCount);
  int64_t x = 0;
  int64_t z = 0;
  for (uint32_t a = 0; a < arcCount; a++) {
    const uint32_t head = in.v();
    const uint32_t n = in.v();
    if (!in.ok || head > 7 || n < 2 || n > in.left() / 2) return false;
    Arc arc;
    arc.kind = static_cast<int>(head & 3);
    arc.far = (head & 4) != 0;
    arc.xz.reserve(2 * static_cast<size_t>(n));
    for (uint32_t i = 0; i < n; i++) {
      x += in.s();
      z += in.s();
      // (nothing lies outside the frame: a file that says so is broken)
      if (!in.ok || x < frame[0] || x > frame[2] || z < frame[1] || z > frame[3]) return false;
      arc.xz.push_back(static_cast<double>(x) * unit);
      arc.xz.push_back(static_cast<double>(z) * unit);
    }
    if (arc.far) {
      // one bit a point: is it part of the far form too?  (An arc's two ends always are.)
      for (uint32_t i = 0; i < n; i += 8) {
        const uint32_t bits = in.u8();
        for (uint32_t k = 0; k < 8 && i + k < n; k++) {
          if (!(bits & (1u << k))) continue;
          arc.farXz.push_back(arc.xz[2 * static_cast<size_t>(i + k)]);
          arc.farXz.push_back(arc.xz[2 * static_cast<size_t>(i + k) + 1]);
        }
      }
      const size_t m = arc.farXz.size();
      const size_t all = arc.xz.size();
      if (!in.ok || m < 4 || arc.farXz[0] != arc.xz[0] || arc.farXz[1] != arc.xz[1] || arc.farXz[m - 2] != arc.xz[all - 2] || arc.farXz[m - 1] != arc.xz[all - 1]) return false;
    }
    arcs.push_back(std::move(arc));
  }

  std::vector<uint8_t> uses(arcs.size(), 0);
  const uint32_t countryCount = in.v();
  if (!in.ok || countryCount < 1 || countryCount > in.left() / 10) return false;
  o.countries.reserve(countryCount);
  for (uint32_t c = 0; c < countryCount; c++) {
    OutlineCountry country;
    for (int i = 0; i < 2; i++) {
      const uint32_t letter = in.u8();
      if (letter < 'A' || letter > 'Z') return false;
      country.iso += static_cast<char>(letter);
    }
    country.nameEn = in.text();
    country.nameDe = in.text();
    country.labelX = static_cast<double>(in.s()) * unit;
    country.labelZ = static_cast<double>(in.s()) * unit;
    if (!in.ok || !readRings(in, arcs, uses, country.rings, country.box)) return false;
    o.countries.push_back(std::move(country));
  }
  const uint32_t lakeCount = in.v();
  if (!in.ok || lakeCount > in.left() / 4) return false;
  o.lakes.reserve(lakeCount);
  for (uint32_t l = 0; l < lakeCount; l++) {
    OutlineLake lake;
    if (!readRings(in, arcs, uses, lake.rings, lake.box)) return false;
    o.lakes.push_back(std::move(lake));
  }
  // the file ends here: one that goes on is another kind of file
  if (!in.ok || in.left() != 0) return false;

  // the lines to stroke: the arcs themselves, without the seams
  for (Arc& arc : arcs) {
    if (arc.kind == 3) continue;
    OutlineLine line;
    line.kind = static_cast<OutlineLine::Kind>(arc.kind);
    line.box = boxOf(arc.xz);
    line.xz = std::move(arc.xz);
    line.far = std::move(arc.farXz);
    o.lines.push_back(std::move(line));
  }
  return true;
}

/**
 * How often a ring goes round a point: 1 for a ring, −1 for a hole (which runs the
 * other way round), 0 for a point outside.
 */
int winding(const std::vector<double>& xz, double x, double z) {
  int w = 0;
  const size_t n = xz.size();
  for (size_t i = 0; i < n; i += 2) {
    const size_t j = i + 2 < n ? i + 2 : 0;
    const double ax = xz[i];
    const double az = xz[i + 1];
    const double bx = xz[j];
    const double bz = xz[j + 1];
    // which side of a→b the point lies on
    const double side = (bx - ax) * (z - az) - (x - ax) * (bz - az);
    if (az <= z) {
      if (bz > z && side > 0) w++;
    } else if (bz <= z && side < 0) {
      w--;
    }
  }
  return w;
}

}  // namespace

WorldOutline parseWorldOutline(const Bytes& bytes) {
  WorldOutline outline;
  if (!readOutline(bytes, outline)) return WorldOutline{};
  return outline;
}

const OutlineCountry* countryAt(const WorldOutline& outline, double x, double z) {
  for (const OutlineCountry& c : outline.countries) {
    if (!inBox(c.box, x, z)) continue;
    // inside: more rings than holes go round the point
    int w = 0;
    for (const OutlineRing& r : c.rings)
      if (inBox(r.box, x, z)) w += winding(r.xz, x, z);
    if (w != 0) return &c;
  }
  return nullptr;
}

const OutlineCountry* countryOfBox(const WorldOutline& outline, const std::array<double, 4>& box) {
  const auto at = [&](double u, double v) { return countryAt(outline, box[0] + u * (box[2] - box[0]), box[1] + v * (box[3] - box[1])); };
  // a region is a town and what lies around it: where its middle is, it belongs
  if (const OutlineCountry* middle = at(0.5, 0.5)) return middle;
  // … unless that is water: then to the country most of 25 points spread over the box lie in
  constexpr int N = 5;
  std::vector<int> points(outline.countries.size(), 0);
  const OutlineCountry* most = nullptr;
  for (int i = 0; i < N; i++) {
    for (int j = 0; j < N; j++) {
      const OutlineCountry* c = at((i + 0.5) / N, (j + 0.5) / N);
      if (!c) continue;
      const int count = ++points[static_cast<size_t>(c - outline.countries.data())];
      if (!most || count > points[static_cast<size_t>(most - outline.countries.data())]) most = c;
    }
  }
  return most;
}

}  // namespace fs
