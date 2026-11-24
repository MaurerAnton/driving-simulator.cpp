// SPDX-License-Identifier: AGPL-3.0-only
/**
 * For the tests of what is drawn of the vehicles (render_car_test, render_actors_test): a tree
 * of objects printed as lines of numbers – every mesh with its geometry and its material –,
 * the way tools/diff/render_cars.ts prints the original's.  Keep the two alike, line by line.
 *
 * Places are printed with 100 added, so that the comparison's relative tolerance is an
 * absolute one for them (0.1 mm at 1e-6); the sums are of the numbers' sizes, so that they
 * do not cancel.
 */
#pragma once

#include <cctype>
#include <string>
#include <vector>

#include "engine/engine.h"
#include "platform/native/canvas_native.h"
#include "tests/dump.h"

namespace fs::rdump {

using dump::out;

/** a text as one word of a line (as the original's script writes them) */
inline std::string word(const std::string& s) {
  std::string w;
  bool gap = false;
  for (const char ch : s) {
    if (std::isspace(static_cast<unsigned char>(ch))) {
      gap = true;
      continue;
    }
    if (gap) w += '_';
    gap = false;
    w += ch;
  }
  if (gap) w += '_';
  return w;
}

inline std::vector<std::string> words(const std::vector<std::string>& list, size_t from = 0) {
  std::vector<std::string> w;
  for (size_t i = from; i < list.size(); i++) w.push_back(word(list[i]));
  return w;
}

/** Σ |v[i]| · (i mod 13 + 1) */
inline double absSum(const BufferAttribute& a, size_t n) {
  double s = 0;
  for (size_t i = 0; i < n; i++) s += std::fabs(a.at(i)) * static_cast<double>((i % 13) + 1);
  return s;
}
inline double absSum(const BufferAttribute& a) { return absSum(a, a.length()); }
inline double absSum(const std::vector<double>& a) {
  double s = 0;
  for (size_t i = 0; i < a.size(); i++) s += std::fabs(a[i]) * static_cast<double>((i % 13) + 1);
  return s;
}

/** a place */
inline double P(double v) { return v + 100; }
inline std::vector<double> P3(const Vector3& v) { return {P(v.x), P(v.y), P(v.z)}; }
inline std::vector<double> PQ(const Quaternion& q) { return {P(q.x), P(q.y), P(q.z), P(q.w)}; }
inline std::vector<double> rgb(const Color& c) { return {c.r, c.g, c.b}; }
inline std::vector<double> xyz(const Vector3& v) { return {v.x, v.y, v.z}; }
inline std::vector<double> places(const Matrix4& m) {
  std::vector<double> e;
  for (int i = 0; i < 16; i++) e.push_back(P(m.elements[i]));
  return e;
}
inline std::vector<double> elements(const Matrix4& m) { return std::vector<double>(m.elements, m.elements + 16); }

inline void geometry(BufferGeometry& g) {
  out("g", static_cast<int>(g.attributes.size()), g.index ? static_cast<int>(g.index->count()) : -1, g.index ? absSum(*g.index) : 0.0);
  for (const auto& entry : g.attributes) out("a", entry.first, entry.second->itemSize, static_cast<int>(entry.second->count()), absSum(*entry.second));
  const AttrPtr position = g.getAttribute("position");
  if (position && position->count() > 0) {
    g.computeBoundingBox();
    out("b", P3(g.boundingBox->min), P3(g.boundingBox->max));
  }
}

inline const char* typeName(const Material& m) {
  switch (m.type) {
    case MaterialType::Basic: return "MeshBasicMaterial";
    case MaterialType::Lambert: return "MeshLambertMaterial";
    case MaterialType::Phong: return "MeshPhongMaterial";
    default: return "?";
  }
}

inline void material(const Material& m) {
  const bool lit = m.type == MaterialType::Lambert || m.type == MaterialType::Phong;
  const bool phong = m.type == MaterialType::Phong;
  const Color none(0.0, 0.0, 0.0);
  out("m", typeName(m), rgb(m.color), m.opacity, m.transparent, m.depthWrite, m.depthTest, static_cast<int>(m.side), m.shadowSide ? static_cast<int>(*m.shadowSide) : -1, m.map ? 1 : 0,
      m.toneMapped, m.fog, m.polygonOffset, m.polygonOffsetFactor, m.polygonOffsetUnits, m.colorWrite, rgb(lit ? m.emissive : none), rgb(phong ? m.specular : none), phong ? m.shininess : 0.0);
  // a picture painted on a canvas: its size, how much was drawn, the texts
  if (m.map && m.map->canvas) {
    const Ctx2D g = m.map->canvas.getContext2D();
    const std::vector<std::string> texts = canvasNative::texts(g);
    out("c", m.map->canvas.width(), m.map->canvas.height(), canvasNative::calls(g), static_cast<int>(texts.size()), words(texts));
  }
}

inline const char* typeName(const Object3D& o) {
  switch (o.type) {
    case ObjectType::Group: return "Group";
    case ObjectType::Scene: return "Scene";
    case ObjectType::Mesh: return "Mesh";
    // (three.js gives an InstancedMesh no type of its own)
    case ObjectType::InstancedMesh: return "Mesh";
    default: return "Object3D";
  }
}

inline bool isMesh(const Object3D& o) { return o.type == ObjectType::Mesh || o.type == ObjectType::InstancedMesh; }

/** An object and everything below it: where each is, and of a mesh its geometry and its material. */
inline void tree(const std::string& name, Object3D& top) {
  out("tree", word(name));
  const std::function<void(Object3D&, int)> visit = [&visit](Object3D& o, int depth) {
    out("o", depth, typeName(o), o.name, P3(o.position), PQ(o.quaternion), xyz(o.scale), o.visible, o.renderOrder, o.castShadow, o.receiveShadow, o.frustumCulled,
        static_cast<int>(o.children.size()));
    if (isMesh(o)) {
      Mesh& mesh = static_cast<Mesh&>(o);
      geometry(*mesh.geometry);
      material(*mesh.material);
      if (o.type == ObjectType::InstancedMesh) {
        InstancedMesh& im = static_cast<InstancedMesh&>(o);
        out("n", im.count, absSum(*im.instanceMatrix, static_cast<size_t>(im.count) * 16), im.instanceColor ? absSum(*im.instanceColor, static_cast<size_t>(im.count) * 3) : -1.0);
      }
    }
    for (const ObjectPtr& c : o.children) visit(*c, depth + 1);
  };
  visit(top, 0);
}

/** The same objects after something was changed: where they are, whether they show, how their materials are set. */
inline void pose(const std::string& name, Object3D& top) {
  out("pose", word(name));
  const std::function<void(Object3D&, int)> visit = [&visit](Object3D& o, int depth) {
    const Material* m = isMesh(o) ? static_cast<Mesh&>(o).material.get() : nullptr;
    std::string line = "p";
    dump::fmt(line, depth);
    dump::fmt(line, P3(o.position));
    dump::fmt(line, PQ(o.quaternion));
    dump::fmt(line, xyz(o.scale));
    dump::fmt(line, o.visible);
    dump::fmt(line, o.renderOrder);
    if (m) {
      dump::fmt(line, m->transparent);
      dump::fmt(line, m->opacity);
      dump::fmt(line, rgb(m->color));
    } else {
      for (int i = 0; i < 5; i++) dump::fmt(line, "-");
    }
    dump::lines().push_back(line);
    for (const ObjectPtr& c : o.children) visit(*c, depth + 1);
  };
  visit(top, 0);
}

}  // namespace fs::rdump
