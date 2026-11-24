// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/yard.ts @ v1.7.7
#include "render/yard.h"

#include <algorithm>
#include <cmath>

#include "core/js.h"
#include "engine/geometries.h"
#include "sim/jsmath.h"

namespace fs {

namespace {

#define FS_MATERIAL(NAME, ...)                                    \
  const MaterialPtr& NAME() {                                     \
    static const MaterialPtr* m = new MaterialPtr(__VA_ARGS__); \
    return *m;                                                    \
  }
FS_MATERIAL(ASPHALT, lambert({.color = Color(0x3b3e42), .polygonOffset = true, .polygonOffsetFactor = -2, .polygonOffsetUnits = -6}))
FS_MATERIAL(PAINT, basic({.color = Color(0xf2f2ee), .polygonOffset = true, .polygonOffsetFactor = -4, .polygonOffsetUnits = -12}))
FS_MATERIAL(GOAL, basic({.color = Color(0x38d06a), .transparent = true, .opacity = 0.32, .depthWrite = false, .polygonOffset = true, .polygonOffsetFactor = -6, .polygonOffsetUnits = -18}))
FS_MATERIAL(CONE, lambert({.color = Color(0xf06a12)}))
FS_MATERIAL(CONE_BAND, lambert({.color = Color(0xf4f4f0)}))
FS_MATERIAL(CONCRETE, lambert({.color = Color(0x9a9d9f)}))
FS_MATERIAL(BUFFER, lambert({.color = Color(0x1b1c1e)}))
#undef FS_MATERIAL

/** the triangles turned the other way round */
std::vector<uint32_t> flipped(const std::vector<uint32_t>& index) {
  static const int turn[3] = {0, 2, 1};
  std::vector<uint32_t> out(index.size());
  for (size_t k = 0; k < index.size(); k++) out[k] = index[k - (k % 3) + static_cast<size_t>(turn[k % 3])];
  return out;
}

}  // namespace

YardScene::YardScene() {
  group->name = "practice yard";
  group->visible = false;
}

bool YardScene::show(const YardFrame& frame, const TaskLayout& layout, const std::function<double(double x, double z)>& heightAt) {
  hide();
  Group& g = *group;
  bool complete = true;
  const auto y = [&](double u, double w) -> double {
    const auto [x, z] = frame.toWorld(u, w);
    const double h = heightAt(x, z);
    if (std::isnan(h)) {
      complete = false;
      return 0;
    }
    return h;
  };
  /** a strip of ground between two lines of points, draped over the terrain */
  const auto drape = [&](double u0, double w0, double u1, double w1, double lift, const MaterialPtr& material, double cell = 2.5) {
    const int nu = std::max(1, static_cast<int>(std::ceil(std::abs(u1 - u0) / cell)));
    const int nw = std::max(1, static_cast<int>(std::ceil(std::abs(w1 - w0) / cell)));
    std::vector<float> pos(static_cast<size_t>((nu + 1) * (nw + 1) * 3));
    for (int j = 0; j <= nw; j++) {
      for (int i = 0; i <= nu; i++) {
        const double u = u0 + ((u1 - u0) * i) / nu;
        const double w = w0 + ((w1 - w0) * j) / nw;
        const auto [x, z] = frame.toWorld(u, w);
        const size_t k = static_cast<size_t>((j * (nu + 1) + i) * 3);
        pos[k] = static_cast<float>(x);
        pos[k + 1] = static_cast<float>(y(u, w) + lift);
        pos[k + 2] = static_cast<float>(z);
      }
    }
    std::vector<uint32_t> index;
    for (int j = 0; j < nw; j++) {
      for (int i = 0; i < nu; i++) {
        const uint32_t a = static_cast<uint32_t>(j * (nu + 1) + i);
        const uint32_t b = a + 1;
        const uint32_t c = a + static_cast<uint32_t>(nu) + 1;
        const uint32_t d = c + 1;
        index.insert(index.end(), {a, c, b, b, c, d});
      }
    }
    auto geo = make<BufferGeometry>();
    geo->setAttribute("position", attr(std::move(pos), 3));
    geo->setIndex(index);
    geo->computeVertexNormals();
    // (whichever way the yard is turned, the ground must face up)
    const AttrPtr n = geo->getAttribute("normal");
    if (n->getY(0) < 0) {
      geo->setIndex(flipped(index));
      geo->computeVertexNormals();
    }
    made_.push_back(geo);
    auto mesh = make<Mesh>(geo, material);
    mesh->receiveShadow = material == ASPHALT();
    g.add(mesh);
  };

  /** any flat piece given by its corner points (u, w) and triangles, draped over the terrain */
  const auto patch = [&](const std::vector<double>& points, const std::vector<uint32_t>& index, double lift, const MaterialPtr& material) {
    std::vector<float> pos((points.size() / 2) * 3);
    for (size_t i = 0; i < points.size(); i += 2) {
      const auto [x, z] = frame.toWorld(points[i], points[i + 1]);
      pos[(i / 2) * 3] = static_cast<float>(x);
      pos[(i / 2) * 3 + 1] = static_cast<float>(y(points[i], points[i + 1]) + lift);
      pos[(i / 2) * 3 + 2] = static_cast<float>(z);
    }
    // (whichever way the points run, the ground must face up)
    const double a[2] = {pos[index[0] * 3], pos[index[0] * 3 + 2]};
    const double b[2] = {pos[index[1] * 3], pos[index[1] * 3 + 2]};
    const double c[2] = {pos[index[2] * 3], pos[index[2] * 3 + 2]};
    const bool up = (b[1] - a[1]) * (c[0] - a[0]) - (b[0] - a[0]) * (c[1] - a[1]) > 0;
    auto geo = make<BufferGeometry>();
    geo->setAttribute("position", attr(std::move(pos), 3));
    geo->setIndex(up ? index : flipped(index));
    geo->computeVertexNormals();
    made_.push_back(geo);
    auto mesh = make<Mesh>(geo, material);
    mesh->receiveShadow = material == ASPHALT();
    g.add(mesh);
  };
  /** a painted line from one point to another, in any direction */
  const auto line = [&](double u0, double w0, double u1, double w1, double width) {
    const double len = jsHypot(u1 - u0, w1 - w0);
    const int n = std::max(1, static_cast<int>(std::ceil(len / 2.5)));
    const double nu = (-(w1 - w0) / len) * (width / 2);
    const double nw = ((u1 - u0) / len) * (width / 2);
    std::vector<double> points;
    std::vector<uint32_t> index;
    for (int i = 0; i <= n; i++) {
      const double u = u0 + ((u1 - u0) * i) / n;
      const double w = w0 + ((w1 - w0) * i) / n;
      points.insert(points.end(), {u + nu, w + nw, u - nu, w - nw});
      const uint32_t k = static_cast<uint32_t>(i * 2);
      if (i < n) index.insert(index.end(), {k, k + 1, k + 2, k + 2, k + 1, k + 3});
    }
    patch(points, index, 0.07, PAINT());
  };

  // the paved areas, a little wider than what the vehicles may use
  for (const Rect& r : layout.allowed) drape(r.u0 - 1.2, r.w0 - 1.2, r.u1 + 1.2, r.w1 + 1.2, 0.05, ASPHALT());
  // the cut-off corners of a junction: triangles, in rows of small ones
  for (const Chamfer& c : layout.chamfers) {
    const double pad = 1.2;
    const double su = jsSign(c.du);
    const double sw = jsSign(c.dw);
    // (as far beyond the cut as the asphalt of the roads reaches beyond their lines)
    const double lu = std::abs(c.du) + pad * 1.45;
    const double lw = std::abs(c.dw) + pad * 1.45;
    const int n = 4;
    std::vector<double> points;
    std::vector<std::vector<uint32_t>> at;
    for (int i = 0; i <= n; i++) {
      at.emplace_back();
      for (int j = 0; j <= n - i; j++) {
        at[static_cast<size_t>(i)].push_back(static_cast<uint32_t>(points.size() / 2));
        points.push_back(c.u + su * ((lu * i) / n));
        points.push_back(c.w + sw * ((lw * j) / n));
      }
    }
    std::vector<uint32_t> index;
    for (size_t i = 0; i < static_cast<size_t>(n); i++) {
      for (size_t j = 0; j < static_cast<size_t>(n) - i; j++) {
        index.insert(index.end(), {at[i][j], at[i + 1][j], at[i][j + 1]});
        if (j < static_cast<size_t>(n) - i - 1) index.insert(index.end(), {at[i + 1][j], at[i + 1][j + 1], at[i][j + 1]});
      }
    }
    patch(points, index, 0.05, ASPHALT());
  }
  // boundary lines: 15 cm of white paint
  for (const auto& [u0, w0, u1, w1] : layout.lines) line(u0, w0, u1, w1, 0.15);
  // where to end up
  if (layout.goalMarked) drape(layout.goal.u0, layout.goal.w0, layout.goal.u1, layout.goal.w1, 0.09, GOAL());

  // cones: orange with a white band
  const GeometryPtr coneGeo = coneGeometry(0.17, 0.55, 10);
  const GeometryPtr bandGeo = cylinderGeometry(0.105, 0.135, 0.12, 10);
  const GeometryPtr baseGeo = boxGeometry(0.4, 0.04, 0.4);
  made_.insert(made_.end(), {coneGeo, bandGeo, baseGeo});
  for (const auto& [u, w] : layout.cones) {
    const auto [x, z] = frame.toWorld(u, w);
    const double h = y(u, w) + 0.05;
    auto cone = make<Mesh>(coneGeo, CONE());
    cone->position.set(x, h + 0.3, z);
    cone->castShadow = true;
    auto band = make<Mesh>(bandGeo, CONE_BAND());
    band->position.set(x, h + 0.3, z);
    auto base = make<Mesh>(baseGeo, CONE());
    base->position.set(x, h + 0.02, z);
    g.add(cone, band, base);
  }

  // the loading ramp: a concrete block with a rubber buffer along its edge
  if (layout.ramp) {
    const RampRect& r = *layout.ramp;
    const auto [x, z] = frame.toWorld((r.u0 + r.u1) / 2, (r.w0 + r.w1) / 2);
    const double h = std::min({y(r.u0, r.w0), y(r.u1, r.w0), y(r.u0, r.w1), y(r.u1, r.w1)});
    const bool turned = frame.yard.rot == 1;
    const GeometryPtr blockGeo = boxGeometry(turned ? r.w1 - r.w0 : r.u1 - r.u0, r.h + 1, turned ? r.u1 - r.u0 : r.w1 - r.w0);
    made_.push_back(blockGeo);
    auto block = make<Mesh>(blockGeo, CONCRETE());
    block->position.set(x, h + (r.h + 1) / 2 - 1, z);
    block->castShadow = true;
    block->receiveShadow = true;
    g.add(block);
    const auto [bx, bz] = frame.toWorld(r.u1 + 0.04, (r.w0 + r.w1) / 2);
    const GeometryPtr bufferGeo = boxGeometry(turned ? r.w1 - r.w0 : 0.08, 0.3, turned ? 0.08 : r.w1 - r.w0);
    made_.push_back(bufferGeo);
    auto buffer = make<Mesh>(bufferGeo, BUFFER());
    buffer->position.set(bx, h + r.h - 0.2, bz);
    g.add(buffer);
  }
  g.visible = true;
  return complete;
}

void YardScene::hide() {
  group->visible = false;
  group->clear();
  for (const GeometryPtr& geo : made_) geo->dispose();
  made_.clear();
}

}  // namespace fs
