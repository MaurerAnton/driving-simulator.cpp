// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/snowtracks.ts @ v1.7.7
#include "render/snowtracks.h"

#include <algorithm>
#include <cmath>

#include "world/types.h"
#include "world/world.h"

namespace fs {

namespace {

/** half the width of a track in the snow (m) */
constexpr double HALF_WIDTH = 0.26;
/** a point every so many metres along a lane */
constexpr double STEP = 4;
/** the tracks lie this much above the road (m) – with the polygon offset, clear of its surface */
constexpr double LIFT = 0.03;
/** the colour of wet asphalt between the snow */
constexpr uint32_t WET_ASPHALT = 0x2e3135;

}  // namespace

SnowTracks::SnowTracks() {
  MaterialPtr material = lambert({
      .color = Color(WET_ASPHALT),
      // (the vertex colours carry the soft edges: alpha 1 in the middle of a track, 0 at its sides)
      .vertexColors = true,
      .transparent = true,
      .depthWrite = false,
      .polygonOffset = true,
      .polygonOffsetFactor = -3,
      .polygonOffsetUnits = -6,
  });
  mesh = make<Mesh>(make<BufferGeometry>(), material);
  mesh->name = "snow tracks";
  // after the road (0) and its markings (1)
  mesh->renderOrder = 2;
  mesh->receiveShadow = true;
  mesh->visible = false;
}

void SnowTracks::dispose() {
  mesh->removeFromParent();
  mesh->geometry->dispose();
  mesh->material->dispose();
}

void SnowTracks::build(double x, double z, const std::vector<const Lane*>& lanes, const std::vector<Connector*>* connectors) {
  cx_ = x;
  cz_ = z;
  const double R = radius;
  std::vector<float> pos;
  std::vector<uint32_t> idx;
  for (const Lane* lane : lanes) {
    if (!lane) continue;
    const Edge* e = lane->edge;
    // no snow in a tunnel; only what is near
    if ((e->flags & EF_TUNNEL) || e->maxX < x - R || e->minX > x + R || e->maxZ < z - R || e->minZ > z + R) continue;
    strip(lane->len, [lane](double d, PolySample& out) -> PolySample& { return lanePoint(*lane, d, out); }, x, z, pos, idx);
  }
  if (connectors) {
    for (const Connector* c : *connectors) {
      if (!c || (c->from->edge->flags & EF_TUNNEL)) continue;
      samplePolyline(c->pl, c->len * 0.5, s_);
      if (std::hypot(s_.x - x, s_.z - z) > R + c->len) continue;
      strip(c->len, [c](double d, PolySample& out) -> PolySample& { return samplePolyline(c->pl, d, out); }, x, z, pos, idx);
    }
  }
  const size_t n = pos.size() / 3;
  auto g = make<BufferGeometry>();
  g->setAttribute("position", attr(std::move(pos), 3));
  std::vector<float> normal(n * 3);
  for (size_t i = 0; i < n; i++) normal[i * 3 + 1] = 1;
  g->setAttribute("normal", attr(std::move(normal), 3));
  // white, its alpha the strip's shape: 0 at the sides, 1 in the middle (three vertices across)
  std::vector<float> colour(n * 4);
  for (size_t i = 0; i < n; i++) {
    colour[i * 4] = colour[i * 4 + 1] = colour[i * 4 + 2] = 1;
    colour[i * 4 + 3] = i % 3 == 1 ? 1 : 0;
  }
  g->setAttribute("color", attr(std::move(colour), 4));
  if (n > 65535) g->setIndex(attr(std::move(idx), 1));
  else {
    std::vector<uint16_t> i16(idx.begin(), idx.end());
    g->setIndex(attr(std::move(i16), 1));
  }
  g->computeBoundingSphere();
  mesh->geometry->dispose();
  mesh->geometry = g;
}

void SnowTracks::strip(double len, const std::function<PolySample&(double, PolySample&)>& at, double x, double z, std::vector<float>& pos, std::vector<uint32_t>& idx) {
  PolySample& s = s_;
  const double reach = radius * 1.15;
  const int n = std::max(1, static_cast<int>(std::ceil(len / STEP)));
  bool joined = false;
  for (int i = 0; i <= n; i++) {
    at((len * i) / n, s);
    if (std::hypot(s.x - x, s.z - z) > reach) {
      joined = false;
      continue;
    }
    const uint32_t base = static_cast<uint32_t>(pos.size() / 3);
    // across the lane (either way: the tracks lie to both sides)
    const double nx = s.tz;
    const double nz = -s.tx;
    for (int side = -1; side <= 1; side += 2) {
      for (int k = -1; k <= 1; k++) {
        const double w = side * HALF_TRACK + k * HALF_WIDTH;
        pos.insert(pos.end(), {static_cast<float>(s.x + nx * w), static_cast<float>(s.y + LIFT), static_cast<float>(s.z + nz * w)});
      }
    }
    if (joined) {
      const uint32_t prev = base - 6;
      for (uint32_t t = 0; t < 2; t++) {
        for (uint32_t k = 0; k < 2; k++) {
          const uint32_t a = prev + t * 3 + k;
          const uint32_t c = base + t * 3 + k;
          idx.insert(idx.end(), {a, c, a + 1, a + 1, c, c + 1});
        }
      }
    }
    joined = true;
  }
}

}  // namespace fs
