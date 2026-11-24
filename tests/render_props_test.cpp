// SPDX-License-Identifier: AGPL-3.0-only
// LINK: render engine world core platform geo
/**
 * The trees, street lamps and parked cars of a tile (render/props, render/parked): how wide a
 * tree is drawn (what tests/world/streetdetail.test.ts asks of treeWidth), the shapes, and the
 * instances a made-up tile gets – how many, where, in which colour, and their detail and number
 * by the distance of the viewer.  The numbers are the original's: v1.7.7 run on the same tile.
 */
#include <cassert>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

#include "engine/engine.h"
#include "render/props.h"
#include "render/tiles.h"
#include "world/tilefmt.h"

using namespace fs;

static bool near(double a, double b, double eps = 1e-9) { return std::fabs(a - b) <= eps; }

/** the tile of the test: 40 trees of three kinds, three lamps and a bench, five parked cars of three kinds */
static std::unique_ptr<LoadedTile> madeUpTile() {
  const double cx = 1000, cz = -500;
  TileHeader header;
  header.v = 2;
  header.ix = 3;
  header.iz = -2;
  header.cx = cx;
  header.cz = cz;
  header.yBase = 200;
  header.size = 500;
  header.terrainN = 2;
  header.terrainMin = 200;
  header.terrainScale = 0.04;
  header.coverN = 1;
  const auto object = [](const char* k, double x, double y, double z) {
    TileObject o;
    o.k = k;
    o.x = x;
    o.y = y;
    o.z = z;
    return o;
  };
  header.objects = {object("lamp", 990, 201, -510), object("bench", 0, 0, 0), object("lamp", 1010, 202, -490), object("lamp", 1020, 203, -480)};
  header.objects[2].yaw = 1.2;
  header.objects[3].yaw = -0.5;
  std::vector<float> trees(40 * 5);
  for (int i = 0; i < 40; i++) {
    const size_t o = static_cast<size_t>(i) * 5;
    trees[o] = static_cast<float>(cx + (i % 8) * 10 - 35);
    trees[o + 1] = static_cast<float>(201 + i * 0.1);
    trees[o + 2] = static_cast<float>(cz + (i / 8) * 12 - 24);
    trees[o + 3] = static_cast<float>(6 + (i % 7));
    trees[o + 4] = static_cast<float>(i < 25 ? 0 : i < 35 ? 1 : 3);
  }
  // (x, z in 2 cm from the middle, y in cm above yBase, the yaw in 1/10000 rad; the kind and the colour in a byte)
  const std::vector<int16_t> parked = {500, -250, 120, 15708, 1000, -250, 130, -15708, -2000, 3000, 50, 0, 0, 0, 0, 31416, 1500, 1500, 10, 100};
  const std::vector<uint8_t> parkedK = {16 * 1 + 0, 16 * 1 + 2, 16 * 3 + 4, 16 * 0 + 11, 16 * 1 + 7};
  auto tile = std::make_unique<LoadedTile>();
  tile->key = tileKey(header.ix, header.iz);
  tile->data = unpackTile(packTile(header, {{"hgt", SectionArray(std::vector<uint16_t>(4))},
                                            {"cov", SectionArray(std::vector<uint8_t>(1))},
                                            {"trees", SectionArray(trees)},
                                            {"parked", SectionArray(parked)},
                                            {"parkedK", SectionArray(parkedK)}}));
  tile->header = tile->data.header;
  tile->group = make<Group>();
  return tile;
}

static InstancedMesh& instanced(const ObjectPtr& o) {
  assert(o->type == ObjectType::InstancedMesh);
  return static_cast<InstancedMesh&>(*o);
}

/** instance `i` of a mesh: the sixteen numbers of its matrix (and its colour) */
static void expectInstance(const InstancedMesh& m, int i, std::initializer_list<double> matrix, std::initializer_list<double> colour = {}) {
  Matrix4 m4;
  m.getMatrixAt(i, m4);
  size_t k = 0;
  for (const double v : matrix) {
    if (!near(m4.elements[k], v, 2e-5 * (1 + std::fabs(v)))) std::printf("%s instance %d element %zu: %.6f, not %.6f\n", m.name.c_str(), i, k, m4.elements[k], v);
    assert(near(m4.elements[k], v, 2e-5 * (1 + std::fabs(v))));
    k++;
  }
  if (colour.size() == 3) {
    Color c;
    m.getColorAt(i, c);
    const double* want = colour.begin();
    assert(near(c.r, want[0], 2e-6) && near(c.g, want[1], 2e-6) && near(c.b, want[2], 2e-6));
  }
}

static void expectSphere(const Sphere& s, double x, double y, double z, double r, double eps = 1e-4) {
  if (!(near(s.center.x, x, eps) && near(s.center.y, y, eps) && near(s.center.z, z, eps) && near(s.radius, r, eps)))
    std::printf("sphere %.6f %.6f %.6f %.6f, not %.6f %.6f %.6f %.6f\n", s.center.x, s.center.y, s.center.z, s.radius, x, y, z, r);
  assert(near(s.center.x, x, eps) && near(s.center.y, y, eps) && near(s.center.z, z, eps) && near(s.radius, r, eps));
}

static size_t corners(const InstancedMesh& m) { return m.geometry->position()->count(); }

static void testTreeWidth() {
  // (tests/world/streetdetail.test.ts: broadleaf crowns as wide as real ones)
  const auto crown = [](double h, double r) { return treeWidth(0, h, r) * 0.6; };
  assert(crown(15, 0.5) > 10 && crown(15, 0.5) < 13);
  assert(crown(25, 1) <= 14 * 1.1);
  assert(crown(7, 0.5) > 5);
  assert(near(treeWidth(0, 15, 0.5), 18.666666666666668) && near(treeWidth(0, 25, 1), 25.66666666666667) && near(treeWidth(0, 7, 0.5), 10.666666666666668));
  assert(near(treeWidth(0, 10, 0.5), 13.666666666666666) && near(treeWidth(0, 2, 0), 5.100000000000001));
  // the other kinds keep their shape's proportions
  assert(near(treeWidth(1, 10, 0), 8.5) && near(treeWidth(2, 10, 0.5), 10.5) && near(treeWidth(3, 4, 0.25), 3.8));
}

static void testTile() {
  PropRenderer props;
  std::unique_ptr<LoadedTile> tile = madeUpTile();
  props.add(*tile);
  assert(tile->group->children.size() == 1);
  const std::vector<ObjectPtr>& made = tile->group->children[0]->children;
  // one mesh for each kind of tree that stands there, the lamps, one for each kind of car
  assert(made.size() == 7);
  const char* names[7] = {"trees0", "trees1", "trees3", "lamps", "parkedCars", "parkedCars", "parkedCars"};
  const int counts[7] = {25, 10, 5, 3, 1, 3, 1};
  for (size_t i = 0; i < 7; i++) {
    assert(made[i]->name == names[i]);
    assert(instanced(made[i]).count == counts[i] && instanced(made[i]).capacity() == counts[i]);
    assert(!made[i]->castShadow);
  }
  InstancedMesh& trees0 = instanced(made[0]);
  InstancedMesh& trees1 = instanced(made[1]);
  InstancedMesh& trees3 = instanced(made[2]);
  InstancedMesh& lamps = instanced(made[3]);
  InstancedMesh& small = instanced(made[4]);
  InstancedMesh& saloons = instanced(made[5]);
  InstancedMesh& van = instanced(made[6]);
  assert(trees0.material != trees1.material && trees1.material != trees3.material && trees0.material->vertexColors);
  assert(small.material == saloons.material && saloons.material == van.material && small.material != lamps.material);

  // the trees: shuffled, turned, as wide as their kind is drawn, 15 cm into the ground, each a little lighter or darker
  expectInstance(trees0, 0, {1.40772, 0, 11.83254, 0, 0, 8, 0, 0, -11.83254, 0, 1.40772, 0, 985, 201.05, -524, 1}, {0.962222, 0.932147, 0.910602});
  expectInstance(trees0, 24, {-9.08729, 0, -5.44835, 0, 0, 7, 0, 0, 5.44835, 0, -9.08729, 0, 965, 201.65001, -512, 1}, {0.920622, 0.930216, 0.87571});
  expectInstance(trees1, 0, {4.20182, 0, 8.59486, 0, 0, 11, 0, 0, -8.59486, 0, 4.20182, 0, 975, 204.15001, -476, 1}, {1.153783, 1.110809, 1.091081});
  expectInstance(trees1, 9, {9.22466, 0, -2.07408, 0, 0, 10, 0, 0, 2.07408, 0, 9.22466, 0, 975, 203.35001, -488, 1}, {1.048465, 1.110399, 1.003267});
  expectInstance(trees3, 0, {3.56766, 0, 5.85615, 0, 0, 7, 0, 0, -5.85615, 0, 3.56766, 0, 1005, 204.45001, -476, 1}, {0.929009, 0.892888, 0.878344});
  expectInstance(trees3, 4, {8.28318, 0, 4.30659, 0, 0, 8, 0, 0, -4.30659, 0, 8.28318, 0, 1015, 204.55, -476, 1}, {0.939995, 0.894512, 0.887689});
  // every tree once: the places of the instances are those of the trees
  {
    std::vector<int> seen(40, 0);
    Matrix4 m4;
    for (InstancedMesh* m : {&trees0, &trees1, &trees3}) {
      for (int k = 0; k < m->count; k++) {
        m->getMatrixAt(k, m4);
        const int i = static_cast<int>(std::lround((m4.elements[12] - 965) / 10) + 8 * std::lround((m4.elements[14] + 524) / 12));
        assert(i >= 0 && i < 40 && near(m4.elements[13], 201 + i * 0.1 - 0.15, 1e-4) && near(m4.elements[5], 6 + (i % 7)));
        assert((i < 25 ? &trees0 : i < 35 ? &trees1 : &trees3) == m);
        // (broadleaf trees wider than the others of their height)
        const double wide = std::hypot(m4.elements[0], m4.elements[2]);
        assert(wide >= m4.elements[5] * 0.85 - 1e-4 && (m == &trees0 ? wide <= 14 * 1.1 / 0.6 + 1e-4 : wide < m4.elements[5] * 1.25));
        seen[static_cast<size_t>(i)]++;
      }
    }
    for (const int n : seen) assert(n == 1);
  }
  assert(trees0.boundingSphere && lamps.boundingSphere && saloons.boundingSphere);
  expectSphere(*trees0.boundingSphere, 997.2833, 206.6252, -510.0095, 45.1498, 2e-3);
  expectSphere(*trees1.boundingSphere, 999.051, 208.8074, -484.5345, 40.7355, 2e-3);
  expectSphere(*trees3.boundingSphere, 1016.11, 208.6977, -476, 24.2216, 2e-3);

  // the lamps: where the tile says, turned to the road; no colours of their own
  expectInstance(lamps, 0, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 990, 201, -510, 1});
  expectInstance(lamps, 1, {std::cos(1.2), 0, -std::sin(1.2), 0, 0, 1, 0, 0, std::sin(1.2), 0, std::cos(1.2), 0, 1010, 202, -490, 1});
  expectInstance(lamps, 2, {0.87758, 0, 0.47943, 0, 0, 1, 0, 0, -0.47943, 0, 0.87758, 0, 1020, 203, -480, 1});
  assert(!lamps.instanceColor && corners(lamps) == 102);
  expectSphere(*lamps.boundingSphere, 1004.9303, 205.4966, -495.4494, 25.1102, 2e-3);

  // the parked cars: by kind, each in its colour
  expectInstance(small, 0, {-1, 0, 0.00001, 0, 0, 1, 0, 0, -0.00001, 0, -1, 0, 1000, 200, -500, 1}, {0.043735, 0.114435, 0.254152});
  expectInstance(saloons, 0, {0, 0, -1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1010, 201.2, -505, 1}, {0.814847, 0.814847, 0.791298});
  expectInstance(saloons, 2, {0.99995, 0, -0.01, 0, 0, 1, 0, 0, 0.01, 0, 0.99995, 0, 1030, 200.10001, -470, 1}, {0.017642, 0.038204, 0.111932});
  expectInstance(van, 0, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 960, 200.5, -440, 1}, {0.341914, 0.361307, 0.381326});
  expectSphere(*small.boundingSphere, 1000, 200.809, -500, 2.2221, 2e-3);
  expectSphere(*saloons.boundingSphere, 1021.5156, 201.5467, -489.7969, 24.0712, 2e-3);
  expectSphere(*van.boundingSphere, 960, 201.5087, -440, 2.7862, 2e-3);

  // near by: every tree in detail, the cars shaped
  props.update(1000, -500);
  assert(trees0.count == 25 && trees1.count == 10 && trees3.count == 5 && lamps.count == 3);
  assert(corners(trees0) == 270 && corners(trees1) == 72 && corners(trees3) == 270);
  assert(corners(small) == 756 && corners(saloons) == 756 && corners(van) == 756);
  const GeometryPtr hi0 = trees0.geometry, hi1 = trees1.geometry, hi3 = trees3.geometry;
  const GeometryPtr small1 = small.geometry, saloon1 = saloons.geometry, van1 = van.geometry;
  // (the shapes are a unit high: the broadleaf tree, the conifer, the fruit tree)
  for (const GeometryPtr& g : {hi0, hi1, hi3, lamps.geometry, small1, saloon1, van1}) {
    assert(!g->index && g->position() && g->normal() && g->color() && !g->uv() && g->attributes.size() == 3);
    g->computeBoundingSphere();
  }
  expectSphere(*hi0->boundingSphere, 0, 0.5, 0, 0.5004, 2e-6);
  expectSphere(*hi1->boundingSphere, 0, 0.5, 0.011389, 0.50158, 2e-6);
  expectSphere(*hi3->boundingSphere, 0, 0.51, 0, 0.511566, 2e-6);
  expectSphere(*lamps.geometry->boundingSphere, 0, 3.614158, -0.75, 3.707123, 2e-6);
  expectSphere(*small1->boundingSphere, 0, 0.755086, 0, 2.179267, 2e-6);
  expectSphere(*saloon1->boundingSphere, 0, 0.740086, 0, 2.48582, 2e-6);
  expectSphere(*van1->boundingSphere, 0, 0.990086, 0, 2.748176, 2e-6);
  // the trunk brown, the crown green and darker below
  {
    const BufferAttribute& c = *hi0->color();
    assert(near(c.getX(0), 0.068478, 2e-6) && near(c.getY(0), 0.043735, 2e-6) && near(c.getZ(0), 0.025187, 2e-6));
    assert(near(c.getX(269), 0.065948, 2e-6) && near(c.getY(269), 0.177589, 2e-6) && near(c.getZ(269), 0.033662, 2e-6));
  }

  // further off: fewer trees and reduced shapes, the cars two boxes
  props.update(1000, 400);
  assert(trees0.count == 8 && trees1.count == 3 && trees3.count == 2 && lamps.count == 3);
  assert(corners(trees0) == 60 && corners(trees1) == 15 && corners(trees3) == 60 && corners(lamps) == 102);
  assert(corners(small) == 72 && corners(saloons) == 72 && corners(van) == 72 && small.count == 1 && saloons.count == 3);
  const GeometryPtr lo0 = trees0.geometry, lo1 = trees1.geometry, lo3 = trees3.geometry;
  const GeometryPtr small0 = small.geometry, saloon0 = saloons.geometry, van0 = van.geometry;
  for (const GeometryPtr& g : {lo0, lo1, lo3, small0, saloon0, van0}) g->computeBoundingSphere();
  expectSphere(*lo0->boundingSphere, 0, 0.6, 0, 0.375037, 2e-6);
  expectSphere(*lo1->boundingSphere, 0, 0.55, 0.020053, 0.503801, 2e-6);
  expectSphere(*lo3->boundingSphere, 0, 0.68, 0, 0.36, 2e-6);
  expectSphere(*small0->boundingSphere, 0, 0.809, 0, 2.222103, 2e-6);
  expectSphere(*saloon0->boundingSphere, 0, 0.79625, 0, 2.522972, 2e-6);
  expectSphere(*van0->boundingSphere, 0, 1.00875, 0, 2.78623, 2e-6);
  props.update(1000, 600);
  assert(trees0.count == 8 && trees1.count == 3 && trees3.count == 2);
  props.update(1000, 1000);
  assert(trees0.count == 4 && trees1.count == 2 && trees3.count == 1 && trees0.geometry == lo0);
  // the low graphics: a fifth of the trees, and plain cars near by too
  props.density = 0.2;
  props.update(1000, -500);
  assert(trees0.count == 5 && trees1.count == 2 && trees3.count == 1 && trees0.geometry == hi0 && trees1.geometry == hi1);
  assert(!props.parked.detail && small.geometry == small0 && saloons.geometry == saloon0 && van.geometry == van0);
  props.density = 0.5;
  props.update(1000, 400);
  assert(trees0.count == 4 && trees1.count == 2 && trees3.count == 1 && trees3.geometry == lo3 && props.parked.detail);
  // (the cars in detail within 380 m, the trees within 420 m)
  props.update(1000, -100);
  assert(trees0.geometry == hi0 && saloons.geometry == saloon0);
  props.update(1000, -130);
  assert(saloons.geometry == saloon1 && van.geometry == van1 && small.geometry == small1);

  // shadows: of the trees only, and of those of tiles that come later as well
  props.setShadows(true);
  assert(trees0.castShadow && trees1.castShadow && trees3.castShadow && !lamps.castShadow && !saloons.castShadow);
  std::unique_ptr<LoadedTile> second = madeUpTile();
  second->key = "4_-2";
  props.add(*second);
  assert(second->group->children[0]->children[0]->castShadow && !second->group->children[0]->children[3]->castShadow);
  props.setShadows(false);
  assert(!trees0.castShadow && !second->group->children[0]->children[1]->castShadow);

  // the season is taught to the four materials of the trees: broadleaf, conifer, the other deciduous kind, fruit trees
  std::vector<Material*> taught;
  std::vector<TreeMaterialKind> kinds;
  props.setSeason([&](Material& m, TreeMaterialKind kind) {
    taught.push_back(&m);
    kinds.push_back(kind);
  });
  assert(taught.size() == 4 && taught[0] == trees0.material.get() && taught[1] == trees1.material.get() && taught[3] == trees3.material.get());
  assert(taught[2] != taught[0] && taught[2] != taught[1] && taught[2] != taught[3]);
  assert(kinds[0] == TreeMaterialKind::Deciduous && kinds[1] == TreeMaterialKind::Evergreen && kinds[2] == TreeMaterialKind::Deciduous && kinds[3] == TreeMaterialKind::Orchard);

  // a tile that went is left alone
  props.remove(*second);
  props.remove(*tile);
  props.density = 1;
  props.update(1000, 1000);
  assert(trees0.count == 13 && trees0.geometry == hi0 && saloons.geometry == saloon1);
  props.remove(*tile);
}

static void testEmptyTile() {
  // a tile with nothing on it: its group, and nothing in it
  PropRenderer props;
  TileHeader header;
  header.v = 2;
  header.size = 500;
  header.terrainN = 2;
  header.coverN = 1;
  LoadedTile tile;
  tile.key = "0_0";
  tile.data = unpackTile(packTile(header, {{"hgt", SectionArray(std::vector<uint16_t>(4))}, {"cov", SectionArray(std::vector<uint8_t>(1))}}));
  tile.header = tile.data.header;
  tile.group = make<Group>();
  props.add(tile);
  assert(tile.group->children.size() == 1 && tile.group->children[0]->children.empty());
  props.update(0, 0);
  props.remove(tile);
}

int main() {
  testTreeWidth();
  testTile();
  testEmptyTile();
  std::printf("render_props_test: ok\n");
  return 0;
}
