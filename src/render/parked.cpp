// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/parked.ts @ v1.7.7
#include "render/parked.h"

#include <cmath>
#include <iterator>

#include "engine/geometries.h"
#include "render/tiles.h"
#include "world/parked.h"

namespace fs {

namespace {

/** A geometry with one colour on all its vertices (multiplied by the car's colour, or – near black, light – not much changed by it). */
GeometryPtr tint(const GeometryPtr& g, uint32_t rgb) {
  GeometryPtr geo = g->index ? g->toNonIndexed() : g;
  const Color c(rgb);
  const size_t n = geo->getAttribute("position")->count();
  std::vector<float> arr(n * 3);
  for (size_t i = 0; i < n; i++) {
    arr[i * 3] = static_cast<float>(c.r);
    arr[i * 3 + 1] = static_cast<float>(c.g);
    arr[i * 3 + 2] = static_cast<float>(c.b);
  }
  geo->setAttribute("color", attr(std::move(arr), 3));
  geo->deleteAttribute("uv");
  return geo;
}

/** Two boxes: the body and the cabin. Faces −z (forward), standing on y = 0. */
GeometryPtr simpleCar(double len, double wid, double hgt, bool van) {
  const GeometryPtr body = tint(boxGeometry(wid, hgt * 0.5, len)->translate(0, 0.18 + hgt * 0.25, 0).shared_from_this(), 0xffffff);
  const double cabLen = van ? len * 0.78 : len * 0.5;
  const GeometryPtr cab =
      tint(boxGeometry(wid * 0.9, hgt * 0.38, cabLen)->translate(0, 0.18 + hgt * 0.5 + hgt * 0.16, van ? len * 0.08 : len * 0.04).shared_from_this(), 0x9aa0a8);
  GeometryPtr g = mergeGeometries({body, cab}, false);
  g->computeVertexNormals();
  return g;
}

/** A shaped body with glass, wheels and lights. */
GeometryPtr detailedCar(double len, double wid, double hgt, bool van) {
  std::vector<GeometryPtr> parts;
  const double sill = 0.3;
  const double belt = hgt * (van ? 0.52 : 0.58);
  // lower body with a sloped bonnet: an extruded side profile (z along the car, y up), −z = front
  const double h = len / 2;
  Shape prof;
  prof.moveTo(-h, sill);
  prof.lineTo(h, sill);
  prof.lineTo(h, belt - 0.05);
  prof.lineTo(h - 0.25, belt);
  prof.lineTo(-h + (van ? 0.35 : 0.85), belt);
  prof.lineTo(-h, belt - 0.12);
  prof.closePath();
  const GeometryPtr lower = extrudeGeometry(prof, {.depth = wid, .bevelEnabled = false});
  // (the shape's x is the car's length: turned so that it runs along z, front at −z, centred across)
  lower->rotateY(-PI / 2).translate(wid / 2, 0, 0);
  parts.push_back(tint(lower, 0xffffff));
  // the glasshouse: tapered, darker
  Shape gl;
  const double g0 = -h + (van ? 0.45 : 1.15);
  const double g1 = h - (van ? 0.15 : 0.45);
  gl.moveTo(g0, belt);
  gl.lineTo(g1, belt);
  gl.lineTo(g1 - (van ? 0.05 : 0.25), hgt - 0.02);
  gl.lineTo(g0 + (van ? 0.2 : 0.55), hgt - 0.02);
  gl.closePath();
  const GeometryPtr glass = extrudeGeometry(gl, {.depth = wid * 0.86, .bevelEnabled = false});
  glass->rotateY(-PI / 2).translate(wid * 0.43, 0, 0);
  parts.push_back(tint(glass, 0x2a3036));
  // roof panel in the body colour
  const double r0 = g0 + (van ? 0.2 : 0.55);
  const double r1 = g1 - (van ? 0.05 : 0.25);
  parts.push_back(tint(boxGeometry(wid * 0.84, 0.05, r1 - r0 - 0.1)->translate(0, hgt - 0.01, (r0 + r1) / 2).shared_from_this(), 0xffffff));
  // wheels
  const double r = 0.31;
  for (const double zf : {-h + 0.75, h - 0.7}) {
    for (const double xs : {-1.0, 1.0}) {
      parts.push_back(tint(cylinderGeometry(r, r, 0.2, 10)->rotateZ(PI / 2).translate(xs * (wid / 2 - 0.08), r, zf).shared_from_this(), 0x161616));
    }
  }
  // lights: front white, back red
  for (const double xs : {-1.0, 1.0}) {
    parts.push_back(tint(boxGeometry(0.32, 0.1, 0.04)->translate(xs * (wid / 2 - 0.25), belt - 0.18, -h - 0.01).shared_from_this(), 0xf4f4ea));
    parts.push_back(tint(boxGeometry(0.3, 0.12, 0.04)->translate(xs * (wid / 2 - 0.22), belt - 0.12, h + 0.01).shared_from_this(), 0xb01010));
  }
  for (GeometryPtr& p : parts)
    if (p->index) p = p->toNonIndexed();
  GeometryPtr g = mergeGeometries(parts, false);
  g->computeVertexNormals();
  return g;
}

}  // namespace

ParkedCarRenderer::ParkedCarRenderer() : material_(lambert({.vertexColors = true})) {
  size_t i = 0;
  for (const auto& m : PARKED_MODELS) simple_.push_back(simpleCar(m.len, m.wid, m.hgt, i++ == 3));
  i = 0;
  for (const auto& m : PARKED_MODELS) detailed_.push_back(detailedCar(m.len, m.wid, m.hgt, i++ == 3));
}

void ParkedCarRenderer::add(LoadedTile& tile, const std::shared_ptr<Group>& group) {
  const TileHeader& h = tile.header;
  const SectionArray* parked = tile.data.get("parked");
  const SectionArray* parkedK = tile.data.get("parkedK");
  const std::vector<ParkedCar> cars = unpackParked(parked ? &parked->i16 : nullptr, parkedK ? &parkedK->u8 : nullptr, h.cx, h.cz, h.yBase);
  if (cars.empty()) return;
  TileCars entry;
  entry.cx = h.cx;
  entry.cz = h.cz;
  Matrix4 m4;
  Quaternion q;
  const Vector3 up(0, 1, 0);
  const Vector3 one(1, 1, 1);
  Vector3 pos;
  Color col;
  const int kinds = static_cast<int>(std::size(PARKED_MODELS));
  for (int kind = 0; kind < kinds; kind++) {
    std::vector<const ParkedCar*> mine;
    for (const ParkedCar& c : cars)
      if (c.model == kind) mine.push_back(&c);
    if (mine.empty()) continue;
    auto mesh = make<InstancedMesh>(simple_[static_cast<size_t>(kind)], material_, static_cast<int>(mine.size()));
    for (size_t i = 0; i < mine.size(); i++) {
      const ParkedCar& c = *mine[i];
      q.setFromAxisAngle(up, c.yaw);
      m4.compose(pos.set(c.x, c.y, c.z), q, one);
      mesh->setMatrixAt(static_cast<int>(i), m4);
      mesh->setColorAt(static_cast<int>(i), col.setHex(PARKED_COLOURS[static_cast<size_t>(c.colour)]));
    }
    mesh->instanceMatrix->needsUpdate();
    if (mesh->instanceColor) mesh->instanceColor->needsUpdate();
    mesh->computeBoundingSphere();
    mesh->name = "parkedCars";
    group->add(mesh);
    entry.meshes.push_back(mesh);
    entry.kinds.push_back(kind);
  }
  perTile_[tile.key] = std::move(entry);
}

void ParkedCarRenderer::remove(const LoadedTile& tile) {
  auto p = perTile_.find(tile.key);
  if (p == perTile_.end()) return;
  for (const auto& m : p->second.meshes) m->dispose();
  perTile_.erase(p);
}

void ParkedCarRenderer::update(double x, double z) {
  for (auto& entry : perTile_) {
    TileCars& p = entry.second;
    const bool near = detail && std::hypot(p.cx - x, p.cz - z) < 380;
    for (size_t i = 0; i < p.meshes.size(); i++) {
      const size_t kind = static_cast<size_t>(p.kinds[i]);
      const GeometryPtr& geo = near ? detailed_[kind] : simple_[kind];
      if (p.meshes[i]->geometry != geo) p.meshes[i]->geometry = geo;
    }
  }
}

}  // namespace fs
