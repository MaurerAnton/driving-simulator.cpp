// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The practice yard laid over the map's ground (origin: src/render/yard.ts
 * @ ff37c5d, v0.7.0).
 *
 * The meshes are vertex-coloured and carry the engine's polygon-offset paint
 * through the Paint material, so the lines lie on the asphalt without z-fighting.
 */
#include "render/yard.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace fs {

namespace {
/** the colours of the original's materials (vertex colours, lit by Struct/Paint) */
const uint32_t COL_ASPHALT = 0x3b3e42;
const uint32_t COL_PAINT = 0xf2f2ee;
const uint32_t COL_GOAL = 0x38d06a;
const uint32_t COL_CONE = 0xf06a12;
const uint32_t COL_CONE_BAND = 0xf4f4f0;
const uint32_t COL_CONCRETE = 0x9a9d9f;
const uint32_t COL_BUFFER = 0x1b1c1e;

/** Vertex colours on a geometry (the port's way of carrying a material colour). */
void tint(GeometryPtr g, uint32_t color) {
  if (!g || !g->position) return;
  Color c;
  c.setHex(color);
  std::vector<double> cols(g->position->count() * 3);
  for (size_t i = 0; i < g->position->count(); i++) {
    cols[i * 3] = c.r;
    cols[i * 3 + 1] = c.g;
    cols[i * 3 + 2] = c.b;
  }
  g->color = std::make_shared<BufferAttribute>(cols, 3);
}
}  // namespace

YardScene::YardScene() {
  group.name = "practice yard";
  group.visible = false;
}

/**
 * Lay a task out.  `heightAt` is the ground of the map (NaN where it is
 * not loaded yet – call again when it is).  Returns false in that case.
 */
bool YardScene::show(const YardFrame& frame, const TaskLayout& layout,
                     const std::function<double(double, double)>& heightAt) {
  hide();
  bool complete = true;
  Object3D* g = &group;
  auto y = [&](double u, double w) {
    auto xz = frame.toWorld(u, w);
    double h = heightAt(xz[0], xz[1]);
    if (std::isnan(h)) {
      complete = false;
      return 0.0;
    }
    return h;
  };
  /** add a mesh to the group and remember it for hide() */
  auto add = [&](std::unique_ptr<Object3D> m, bool paint) {
    Object3D* raw = g->add(std::move(m));
    if (auto* mesh = dynamic_cast<Mesh*>(raw)) {
      mesh->kind = paint ? MaterialKind::Paint : MaterialKind::Struct;
      mesh->receiveShadow = paint;
    }
    made_.emplace_back();   // (the group owns its children; made_ only counts them)
    return raw;
  };

  /** a strip of ground between two lines of points, draped over the terrain */
  auto drape = [&](double u0, double w0, double u1, double w1, double lift, uint32_t colour,
                   double cell = 2.5) {
    int nu = std::max(1, static_cast<int>(std::ceil(std::fabs(u1 - u0) / cell)));
    int nw = std::max(1, static_cast<int>(std::ceil(std::fabs(w1 - w0) / cell)));
    std::vector<double> pos((nu + 1) * (nw + 1) * 3);
    for (int j = 0; j <= nw; j++) {
      for (int i = 0; i <= nu; i++) {
        double u = u0 + ((u1 - u0) * i) / static_cast<double>(nu);
        double w = w0 + ((w1 - w0) * j) / static_cast<double>(nw);
        auto xz = frame.toWorld(u, w);
        size_t k = (static_cast<size_t>(j) * (nu + 1) + i) * 3;
        pos[k] = xz[0];
        pos[k + 1] = y(u, w) + lift;
        pos[k + 2] = xz[1];
      }
    }
    std::vector<double> index;
    for (int j = 0; j < nw; j++) {
      for (int i = 0; i < nu; i++) {
        double a = j * (nu + 1) + i;
        double b = a + 1;
        double c = a + nu + 1;
        double d = c + 1;
        index.push_back(a); index.push_back(c); index.push_back(b);
        index.push_back(b); index.push_back(c); index.push_back(d);
      }
    }
    auto geo = std::make_shared<BufferGeometry>();
    geo->setAttribute("position", std::make_shared<BufferAttribute>(pos, 3));
    geo->setAttribute("index", std::make_shared<BufferAttribute>(index, 1));
    geo->computeVertexNormals();
    // (whichever way the yard is turned, the ground must face up)
    if (geo->normal && geo->normal->getY(0) < 0) {
      for (size_t k = 0; k < index.size(); k += 3) std::swap(index[k + 1], index[k + 2]);
      geo->index = std::make_shared<BufferAttribute>(index, 1);
      geo->computeVertexNormals();
    }
    tint(geo, colour);
    auto mesh = std::make_unique<Mesh>(geo, MaterialKind::Paint);
    mesh->receiveShadow = true;
    g->add(std::unique_ptr<Object3D>(mesh.release()));
    made_.emplace_back();
  };

  /** any flat piece given by its corner points (u, w) and triangles, draped over the terrain */
  auto patch = [&](const std::vector<double>& points, const std::vector<double>& index,
                   double lift, uint32_t colour) {
    std::vector<double> pos((points.size() / 2) * 3);
    for (size_t i = 0; i < points.size(); i += 2) {
      auto xz = frame.toWorld(points[i], points[i + 1]);
      pos[(i / 2) * 3] = xz[0];
      pos[(i / 2) * 3 + 1] = y(points[i], points[i + 1]) + lift;
      pos[(i / 2) * 3 + 2] = xz[1];
    }
    // (whichever way the points run, the ground must face up)
    double ax = pos[static_cast<size_t>(index[0]) * 3], az = pos[static_cast<size_t>(index[0]) * 3 + 2];
    double bx = pos[static_cast<size_t>(index[1]) * 3], bz = pos[static_cast<size_t>(index[1]) * 3 + 2];
    double cx = pos[static_cast<size_t>(index[2]) * 3], cz = pos[static_cast<size_t>(index[2]) * 3 + 2];
    bool up = (bz - az) * (cx - ax) - (bx - ax) * (cz - az) > 0;
    std::vector<double> idx = index;
    if (!up)
      for (size_t k = 0; k < idx.size(); k += 3) std::swap(idx[k + 1], idx[k + 2]);
    auto geo = std::make_shared<BufferGeometry>();
    geo->setAttribute("position", std::make_shared<BufferAttribute>(pos, 3));
    geo->setAttribute("index", std::make_shared<BufferAttribute>(idx, 1));
    geo->computeVertexNormals();
    tint(geo, colour);
    auto mesh = std::make_unique<Mesh>(geo, MaterialKind::Paint);
    mesh->receiveShadow = true;
    g->add(std::unique_ptr<Object3D>(mesh.release()));
    made_.emplace_back();
  };

  /** a painted line from one point to another, in any direction */
  auto line = [&](double u0, double w0, double u1, double w1, double width) {
    double len = std::hypot(u1 - u0, w1 - w0);
    int n = std::max(1, static_cast<int>(std::ceil(len / 2.5)));
    double nu = (-(w1 - w0) / len) * (width / 2);
    double nw = ((u1 - u0) / len) * (width / 2);
    std::vector<double> points;
    std::vector<double> index;
    for (int i = 0; i <= n; i++) {
      double u = u0 + ((u1 - u0) * i) / static_cast<double>(n);
      double w = w0 + ((w1 - w0) * i) / static_cast<double>(n);
      points.push_back(u + nu); points.push_back(w + nw);
      points.push_back(u - nu); points.push_back(w - nw);
      if (i < n) {
        index.push_back(i * 2); index.push_back(i * 2 + 1); index.push_back(i * 2 + 2);
        index.push_back(i * 2 + 2); index.push_back(i * 2 + 1); index.push_back(i * 2 + 3);
      }
    }
    patch(points, index, 0.07, COL_PAINT);
  };

  // the paved areas, a little wider than what the vehicles may use
  for (const Rect& r : layout.allowed)
    drape(r.u0 - 1.2, r.w0 - 1.2, r.u1 + 1.2, r.w1 + 1.2, 0.05, COL_ASPHALT);
  // the cut-off corners of a junction: triangles, in rows of small ones
  for (const Chamfer& c : layout.chamfers) {
    double pad = 1.2;
    double su = c.du < 0 ? -1 : (c.du > 0 ? 1 : 0);
    double sw = c.dw < 0 ? -1 : (c.dw > 0 ? 1 : 0);
    // (as far beyond the cut as the asphalt of the roads reaches beyond their lines)
    double lu = std::fabs(c.du) + pad * 1.45;
    double lw = std::fabs(c.dw) + pad * 1.45;
    int n = 4;
    std::vector<double> points;
    std::vector<std::vector<double>> at;
    for (int i = 0; i <= n; i++) {
      at.emplace_back();
      for (int j = 0; j <= n - i; j++) {
        at[i].push_back(points.size() / 2);
        points.push_back(c.u + su * ((lu * i) / n));
        points.push_back(c.w + sw * ((lw * j) / n));
      }
    }
    std::vector<double> index;
    for (int i = 0; i < n; i++) {
      for (int j = 0; j < n - i; j++) {
        index.push_back(at[i][j]); index.push_back(at[i + 1][j]); index.push_back(at[i][j + 1]);
        if (j < n - i - 1) {
          index.push_back(at[i + 1][j]); index.push_back(at[i + 1][j + 1]); index.push_back(at[i][j + 1]);
        }
      }
    }
    patch(points, index, 0.05, COL_ASPHALT);
  }
  // boundary lines: 15 cm of white paint
  for (const auto& l : layout.lines) line(l[0], l[1], l[2], l[3], 0.15);
  // where to end up
  if (layout.goalMarked)
    drape(layout.goal.u0, layout.goal.w0, layout.goal.u1, layout.goal.w1, 0.09, COL_GOAL);

  // cones: orange with a white band
  GeometryPtr coneGeo = makeConeGeometry(0.17, 0.55, 10);
  GeometryPtr bandGeo = makeCylinderGeometry(0.105, 0.135, 0.12, 10);
  GeometryPtr baseGeo = makeBoxGeometry(0.4, 0.04, 0.4);
  tint(coneGeo, COL_CONE);
  tint(bandGeo, COL_CONE_BAND);
  tint(baseGeo, COL_CONE);
  for (const auto& c : layout.cones) {
    auto xz = frame.toWorld(c[0], c[1]);
    double h = y(c[0], c[1]) + 0.05;
    auto cone = std::make_unique<Mesh>(coneGeo, MaterialKind::Struct);
    cone->position.set(xz[0], h + 0.3, xz[1]);
    cone->castShadow = true;
    g->add(std::unique_ptr<Object3D>(cone.release()));
    auto band = std::make_unique<Mesh>(bandGeo, MaterialKind::Struct);
    band->position.set(xz[0], h + 0.3, xz[1]);
    g->add(std::unique_ptr<Object3D>(band.release()));
    auto base = std::make_unique<Mesh>(baseGeo, MaterialKind::Struct);
    base->position.set(xz[0], h + 0.02, xz[1]);
    g->add(std::unique_ptr<Object3D>(base.release()));
    made_.emplace_back();
    made_.emplace_back();
    made_.emplace_back();
  }

  // the loading ramp: a concrete block with a rubber buffer along its edge
  if (layout.hasRamp) {
    const Rect& r = layout.ramp;
    auto xz = frame.toWorld((r.u0 + r.u1) / 2, (r.w0 + r.w1) / 2);
    double h = std::min({y(r.u0, r.w0), y(r.u1, r.w0), y(r.u0, r.w1), y(r.u1, r.w1)});
    bool turned = frame.yard.rot == 1;
    GeometryPtr blockGeo =
        makeBoxGeometry(turned ? r.w1 - r.w0 : r.u1 - r.u0, layout.rampH + 1,
                        turned ? r.u1 - r.u0 : r.w1 - r.w0);
    tint(blockGeo, COL_CONCRETE);
    auto block = std::make_unique<Mesh>(blockGeo, MaterialKind::Struct);
    block->position.set(xz[0], h + (layout.rampH + 1) / 2 - 1, xz[1]);
    block->castShadow = true;
    block->receiveShadow = true;
    g->add(std::unique_ptr<Object3D>(block.release()));
    made_.emplace_back();
    auto bxz = frame.toWorld(r.u1 + 0.04, (r.w0 + r.w1) / 2);
    GeometryPtr bufferGeo =
        makeBoxGeometry(turned ? r.w1 - r.w0 : 0.08, 0.3, turned ? 0.08 : r.w1 - r.w0);
    tint(bufferGeo, COL_BUFFER);
    auto buffer = std::make_unique<Mesh>(bufferGeo, MaterialKind::Struct);
    buffer->position.set(bxz[0], h + layout.rampH - 0.2, bxz[1]);
    g->add(std::unique_ptr<Object3D>(buffer.release()));
    made_.emplace_back();
  }
  g->visible = true;
  return complete;
}

void YardScene::hide() {
  group.visible = false;
  group.children.clear();
  made_.clear();
}

}  // namespace fs
