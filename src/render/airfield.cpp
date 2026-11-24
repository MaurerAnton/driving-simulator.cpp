// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/airfield.ts @ v1.7.7
#include "render/airfield.h"

#include <algorithm>
#include <cmath>

#include "engine/curves.h"
#include "engine/geometries.h"
#include "platform/canvas.h"
#include "sim/airfield.h"

namespace fs {

namespace {

MaterialPtr surfaceMat(uint32_t hex, double offset) {
  return lambert({.color = Color(hex), .polygonOffset = true, .polygonOffsetFactor = offset, .polygonOffsetUnits = offset});
}

const MaterialPtr& ASPHALT() {
  static const MaterialPtr m = surfaceMat(0x4b4d51, -2);
  return m;
}
const MaterialPtr& CONCRETE() {
  static const MaterialPtr m = surfaceMat(0x8e8c86, -2);
  return m;
}
const MaterialPtr& GRASS() {
  static const MaterialPtr m = surfaceMat(0x86a35a, -2);
  return m;
}
const MaterialPtr& WHITE() {
  static const MaterialPtr m = surfaceMat(0xf2f2ee, -4);
  return m;
}
const MaterialPtr& YELLOW() {
  static const MaterialPtr m = surfaceMat(0xe8b818, -4);
  return m;
}

/**
 * Quads along a line from (x1, z1) to (x2, z2): `from`…`to` metres along it,
 * `left`…`right` across it, every `step` metres along and at most `across`
 * metres apart across it (a wide runway is crowned: it follows the ground).
 */
void band(std::vector<float>& out, const AirfieldRenderer::Height& ground, double x1, double z1, double x2, double z2, double from, double to, double left, double right,
          double lift, double step = 20, double across = 6) {
  double len = std::hypot(x2 - x1, z2 - z1);
  if (len == 0) len = 1;
  const double ux = (x2 - x1) / len;
  const double uz = (z2 - z1) / len;
  // right of the direction (x east, z south): (−uz, ux)
  const double rx = -uz;
  const double rz = ux;
  const int n = std::max(1, static_cast<int>(std::ceil((to - from) / step)));
  const int m = std::max(1, static_cast<int>(std::ceil((right - left) / across)));
  auto pt = [&](double a, double c, float* p) {
    const double x = x1 + ux * a + rx * c;
    const double z = z1 + uz * a + rz * c;
    p[0] = static_cast<float>(x);
    p[1] = static_cast<float>(ground(x, z) + lift);
    p[2] = static_cast<float>(z);
  };
  float p00[3], p01[3], p10[3], p11[3];
  auto push = [&](const float* p) { out.insert(out.end(), p, p + 3); };
  for (int i = 0; i < n; i++) {
    const double a0 = from + ((to - from) * i) / n;
    const double a1 = from + ((to - from) * (i + 1)) / n;
    for (int j = 0; j < m; j++) {
      const double c0 = left + ((right - left) * j) / m;
      const double c1 = left + ((right - left) * (j + 1)) / m;
      pt(a0, c0, p00);
      pt(a0, c1, p01);
      pt(a1, c0, p10);
      pt(a1, c1, p11);
      // (facing up: counter-clockwise seen from above – along the line, then across to its right is clockwise)
      push(p00), push(p01), push(p10), push(p01), push(p11), push(p10);
    }
  }
}

std::shared_ptr<Mesh> mesh(std::vector<float> pos, const MaterialPtr& mat) {
  if (pos.empty()) return nullptr;
  auto g = std::make_shared<BufferGeometry>();
  g->setAttribute("position", attr(std::move(pos), 3));
  g->computeVertexNormals();
  auto m = make<Mesh>(g, mat);
  m->receiveShadow = true;
  return m;
}

/** The number of a runway end, painted: white figures, read by whoever lands there. */
std::shared_ptr<Mesh> designation(const std::string& text) {
  const Canvas c(128, 192);
  const Ctx2D g = c.getContext2D();
  g.fillStyle("#ffffff");
  g.font("bold 150px Arial, sans-serif");
  g.textAlign("center");
  g.textBaseline("middle");
  g.fillText(text, 64, 100, 124);
  TexturePtr tex = canvasTexture(c);
  tex->srgb = true;
  MaterialPtr mat = lambert({.map = tex, .transparent = true, .depthWrite = false, .polygonOffset = true, .polygonOffsetFactor = -4, .polygonOffsetUnits = -4});
  // 9 m wide, 18 m long, flat on the ground, the figures' foot towards the threshold
  GeometryPtr geo = planeGeometry(9, 18);
  geo->rotateX(-PI / 2);
  return make<Mesh>(geo, mat);
}

bool concrete(const std::string& s) { return s.find("concrete") != std::string::npos; }

}  // namespace

AirfieldRenderer::AirfieldRenderer(Height ground, std::function<bool(double x, double z)> detailed) : ground(std::move(ground)), detailed(std::move(detailed)) {
  group->name = "airfields";
}

void AirfieldRenderer::set(std::vector<AirfieldJson> fields_) {
  fields = std::move(fields_);
  for (auto& [i, s] : shown) drop(s.root);
  shown.clear();
}

void AirfieldRenderer::update(double x, double z) {
  for (int i = 0; i < static_cast<int>(fields.size()); i++) {
    const AirfieldJson& f = fields[i];
    const double d = std::hypot(f.x - x, f.z - z);
    auto have = shown.find(i);
    if (d > radius) {
      if (have != shown.end()) {
        drop(have->second.root);
        shown.erase(have);
      }
      continue;
    }
    // (made again as more of the ground under it comes to be known in detail – the tiles load around the camera)
    const int k = d < 2500 ? known(f) : 0;
    if (have != shown.end() && k <= have->second.known) continue;
    if (have != shown.end()) drop(have->second.root);
    auto root = build(f);
    group->add(root);
    shown[i] = {root, k};
  }
}

int AirfieldRenderer::known(const AirfieldJson& f) const {
  int n = 0;
  for (const auto& r : f.runways) {
    const double len = std::hypot(r.x2 - r.x1, r.z2 - r.z1);
    const int k = std::max(1, static_cast<int>(std::ceil(len / 200)));
    for (int i = 0; i <= k; i++)
      if (detailed(r.x1 + ((r.x2 - r.x1) * i) / k, r.z1 + ((r.z2 - r.z1) * i) / k)) n++;
  }
  for (const auto& t : f.taxiways)
    if (t.p.size() >= 2 && detailed(t.p[0], t.p[1])) n++;
  for (const auto& a : f.aprons)
    if (a.p.size() >= 2 && detailed(a.p[0], a.p[1])) n++;
  return n;
}

void AirfieldRenderer::drop(const std::shared_ptr<Group>& root) {
  group->remove(root);
  root->traverse([](Object3D& o) {
    if (o.type == ObjectType::Mesh) {
      auto& m = static_cast<Mesh&>(o);
      m.geometry->dispose();
      if (m.material && m.material->map) {
        m.material->map->dispose();
        m.material->dispose();
      }
    }
  });
}

std::shared_ptr<Group> AirfieldRenderer::build(const AirfieldJson& f) {
  auto root = make<Group>();
  root->name = !f.name.empty() ? f.name : f.icao;
  const Height& g = ground;
  Surfaces surf;
  for (const auto& a : f.aprons) {
    if (a.p.size() < 6) continue;
    std::vector<Vector2> pts;
    for (size_t i = 0; i + 1 < a.p.size(); i += 2) pts.emplace_back(a.p[i], a.p[i + 1]);
    if (pts.size() > 3 && std::hypot(pts[0].x - pts.back().x, pts[0].y - pts.back().y) < 0.5) pts.pop_back();
    const auto tris = ShapeUtils::triangulateShape(pts);
    std::vector<float>& out = paved(a.s) ? (concrete(a.s) ? surf.concrete : surf.asphalt) : surf.grass;
    for (const auto& t : tris) {
      for (const Vector2* q : {&pts[t[0]], &pts[t[2]], &pts[t[1]]}) {
        out.push_back(static_cast<float>(q->x));
        out.push_back(static_cast<float>(g(q->x, q->y) + 0.07));
        out.push_back(static_cast<float>(q->y));
      }
    }
  }
  for (const auto& t : f.taxiways) {
    std::vector<float>& out = paved(t.s) ? surf.asphalt : surf.grass;
    for (size_t i = 0; i + 3 < t.p.size(); i += 2) {
      const double x1 = t.p[i], z1 = t.p[i + 1], x2 = t.p[i + 2], z2 = t.p[i + 3];
      const double len = std::hypot(x2 - x1, z2 - z1);
      if (len < 0.5) continue;
      // (a little longer at both ends: the joints close)
      band(out, g, x1, z1, x2, z2, -t.w / 2, len + t.w / 2, -t.w / 2, t.w / 2, 0.08);
      if (paved(t.s)) band(surf.yellow, g, x1, z1, x2, z2, 0, len, -0.15, 0.15, 0.1, 10);
    }
  }
  for (const auto& r : f.runways) runway(r, surf, *root);
  const std::pair<std::vector<float>*, const MaterialPtr*> kinds[] = {
      {&surf.asphalt, &ASPHALT()}, {&surf.concrete, &CONCRETE()}, {&surf.grass, &GRASS()}, {&surf.white, &WHITE()}, {&surf.yellow, &YELLOW()}};
  for (const auto& [pos, mat] : kinds) {
    auto m = mesh(std::move(*pos), *mat);
    if (m) root->add(m);
  }
  return root;
}

void AirfieldRenderer::runway(const RunwayJson& r, Surfaces& surf, Group& root) {
  const Height& g = ground;
  const double len = std::hypot(r.x2 - r.x1, r.z2 - r.z1);
  if (len < 10) return;
  const double hw = r.w / 2;
  const bool isPaved = paved(r.s);
  band(isPaved ? (concrete(r.s) ? surf.concrete : surf.asphalt) : surf.grass, g, r.x1, r.z1, r.x2, r.z2, 0, len, -hw, hw, 0.09, 10);
  if (!isPaved) return;
  std::vector<float>& W = surf.white;
  const double lift = 0.11;
  // the edges
  band(W, g, r.x1, r.z1, r.x2, r.z2, 0, len, -hw + 0.3, -hw + 1.2, lift, 10);
  band(W, g, r.x1, r.z1, r.x2, r.z2, 0, len, hw - 1.2, hw - 0.3, lift, 10);
  // the thresholds: bars along the runway at each end
  const int bars = std::max(4, std::min(16, static_cast<int>(std::floor(r.w / 3.6)) & ~1));
  for (const bool atEnd : {false, true}) {
    const double a0 = atEnd ? len - 36 : 6;
    for (int b = 0; b < bars; b++) {
      const double c0 = -hw + 2 + ((r.w - 4) * b) / bars;
      const double c1 = c0 + ((r.w - 4) / bars) * 0.6;
      band(W, g, r.x1, r.z1, r.x2, r.z2, a0, a0 + 30, c0, c1, lift, 10);
    }
  }
  // the middle line: 30 m dashes, 20 m gaps, between the numbers
  for (double a = 80; a + 30 < len - 80; a += 50) band(W, g, r.x1, r.z1, r.x2, r.z2, a, a + 30, -0.45, 0.45, lift, 10);
  // the numbers of the ends, read from the threshold looking along the runway
  const double ux = (r.x2 - r.x1) / len;
  const double uz = (r.z2 - r.z1) / len;
  for (const bool second : {false, true}) {
    auto d = designation(runwayEnd(r, second));
    const double along = second ? len - 52 : 52;
    const double x = r.x1 + ux * along;
    const double z = r.z1 + uz * along;
    d->position.set(x, g(x, z) + lift + 0.01, z);
    // (the top of the figures points the way one lands: along the runway from that end)
    const double dx = second ? -ux : ux;
    const double dz = second ? -uz : uz;
    d->rotation.y = std::atan2(-dx, -dz);
    root.add(d);
  }
}

}  // namespace fs
