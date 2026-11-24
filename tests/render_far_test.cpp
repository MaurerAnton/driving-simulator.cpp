// SPDX-License-Identifier: AGPL-3.0-only
// LINK: render engine world core platform geo
/**
 * The far ground (render/far), the measured details of walls (render/facadedetail – what
 * tests/world/facadedetail.test.ts asks of the game's side) and the lights at night
 * (render/headlights): which chunks of the far ground are made around a camera and how they are
 * built, the walls marked and the squares of details with their levels of detail, the pool of
 * light, the spotlights and the glowing points of the traffic.  The numbers are the original's:
 * v1.7.7 run on the same made-up data.
 */
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

#include "engine/engine.h"
#include "render/facadedetail.h"
#include "render/far.h"
#include "render/headlights.h"
#include "world/far.h"
#include "world/tilefmt.h"

using namespace fs;

static bool near(double a, double b, double eps = 1e-9) { return std::fabs(a - b) <= eps; }

static void expectNumbers(const BufferAttribute& a, size_t from, std::initializer_list<double> want, const char* what, double eps = 2e-6) {
  size_t i = from;
  for (const double v : want) {
    if (!near(a.at(i), v, eps * (1 + std::fabs(v)))) std::printf("%s[%zu]: %.7f, not %.7f\n", what, i, a.at(i), v);
    assert(near(a.at(i), v, eps * (1 + std::fabs(v))));
    i++;
  }
}

static void expectVector(const Vector3& v, double x, double y, double z, double eps = 2e-6) {
  if (!(near(v.x, x, eps) && near(v.y, y, eps) && near(v.z, z, eps))) std::printf("vector %.6f %.6f %.6f, not %.6f %.6f %.6f\n", v.x, v.y, v.z, x, y, z);
  assert(near(v.x, x, eps) && near(v.y, y, eps) && near(v.z, z, eps));
}

static void expectSphere(const Sphere& s, double x, double y, double z, double r, double eps = 2e-5) {
  expectVector(s.center, x, y, z, eps);
  if (!near(s.radius, r, eps)) std::printf("radius %.6f, not %.6f\n", s.radius, r);
  assert(near(s.radius, r, eps));
}

// ---------------------------------------------------------------------------------------------
// the far ground

/** a made-up region: 200 × 130 points every 100 m from (-4000, -3000); a slope with a hole in it, the cover in stripes */
static FarGround madeUpGround() {
  FarGround f;
  f.minX = -4000;
  f.minZ = -3000;
  f.cell = 100;
  f.w = 200;
  f.h = 130;
  f.base = 150;
  f.step = 0.05;
  f.heights.resize(static_cast<size_t>(f.w * f.h));
  f.cover.resize(static_cast<size_t>(f.w * f.h));
  for (int j = 0; j < f.h; j++) {
    for (int i = 0; i < f.w; i++) {
      const size_t k = static_cast<size_t>(j * f.w + i);
      // (the hole takes all of chunk 3_2, its rim too)
      f.heights[k] = static_cast<uint16_t>(i >= 96 && i <= 128 && j >= 64 && j <= 96 ? FAR_NONE : 1000 + i * 7 + j * 3 + ((i * 13 + j * 7) % 11));
      f.cover[k] = static_cast<uint8_t>((i + 2 * j) % 20);
    }
  }
  return f;
}

/** the chunks that are there, as "i_j:step" in the order of their keys (the step from the number of their corners) */
static std::string chunks(const FarTerrain& far) {
  std::vector<std::string> keys;
  for (const ObjectPtr& o : far.group->children) {
    assert(o->type == ObjectType::Mesh && !o->matrixAutoUpdate && !o->castShadow && !o->receiveShadow);
    const Mesh& m = static_cast<const Mesh&>(*o);
    const size_t corners = m.geometry->position()->count();
    const size_t indices = m.geometry->index->count();
    const int step = corners == 1221 ? 1 : corners == 357 ? 2 : corners == 117 ? 4 : 0;
    assert(step && indices == (step == 1 ? 6912u : step == 2 ? 1920u : 576u));
    // (a chunk lies where its first point lies; its corners are counted from there)
    const int i = static_cast<int>(std::lround((m.position.x + 4000) / 3200));
    const int j = static_cast<int>(std::lround((m.position.z + 3000) / 3200));
    assert(m.position.x == -4000 + i * 3200 && m.position.y == 0 && m.position.z == -3000 + j * 3200 && m.matrix.elements[12] == m.position.x);
    keys.push_back(std::to_string(i) + "_" + std::to_string(j) + ":" + std::to_string(step));
  }
  std::sort(keys.begin(), keys.end());
  std::string out;
  for (const std::string& k : keys) out += (out.empty() ? "" : " ") + k;
  return out;
}

static const Mesh& chunkAt(const FarTerrain& far, int i, int j) {
  for (const ObjectPtr& o : far.group->children)
    if (o->position.x == -4000 + i * 3200 && o->position.z == -3000 + j * 3200) return static_cast<const Mesh&>(*o);
  assert(false && "no such chunk");
  return static_cast<const Mesh&>(*far.group->children[0]);
}

static void testFar() {
  FarTerrain far;
  assert(far.group->name == "far ground" && far.radius == 12000 && far.inner == 1200 && !far.ready() && !far.data);
  far.update(0, 0);
  assert(far.group->children.empty());
  far.set(madeUpGround());
  assert(far.ready() && far.data && far.data->w == 200 && far.group->children.empty());

  // everything within reach at once: in full near by, every other point from 5 km on; nothing where there is no ground at all
  far.update(1000, 500, 100);
  assert(far.group->children.size() == 26);
  assert(chunks(far) == "0_0:1 0_1:1 0_2:1 0_3:2 0_4:2 1_0:1 1_1:1 1_2:1 1_3:2 1_4:2 2_0:1 2_1:1 2_2:1 2_3:2 2_4:2 3_0:1 3_1:1 3_3:2 3_4:2 4_0:2 4_1:2 4_2:2 4_3:2 5_0:2 5_1:2 5_2:2");
  const Mesh& first = chunkAt(far, 0, 0);
  const Material& mat = *first.material;
  // the material: both sides, drawn behind the tiles, the captured shader that leaves out what lies near the camera
  assert(mat.type == MaterialType::Lambert && mat.vertexColors && mat.side == Side::Double && mat.polygonOffset && mat.polygonOffsetFactor == 2 && mat.polygonOffsetUnits == 4);
  assert(mat.patch && mat.patch->key == "fs-far" && mat.patch->fragmentSource.find("if (distance(vFarXZ, uCam) < uInner) discard;") != std::string::npos);
  const Uniform& uInner = *mat.patch->uniforms.at("uInner");
  const Uniform& uCam = *mat.patch->uniforms.at("uCam");
  assert(std::get<double>(uInner.value) == 1200 && std::get<Vector2>(uCam.value).x == 1000 && std::get<Vector2>(uCam.value).y == 500);
  for (const ObjectPtr& o : far.group->children) assert(static_cast<const Mesh&>(*o).material.get() == &mat);
  {
    // a chunk: 33 × 33 points 1.5 m below the ground, then its rim once more 40 m lower (and room for four more)
    const BufferGeometry& g = *first.geometry;
    assert(g.attributes.size() == 3 && g.index->type == AttrType::U16 && g.boundingSphere);
    expectSphere(*g.boundingSphere, 1600, 107.349998, 1600, 2265.286742, 2e-4);
    expectNumbers(*g.position(), 0, {0, 198.5, 0, 100, 198.949997, 0, 200, 199.399994, 0}, "position");
    expectNumbers(*g.position(), 33 * 33 * 3 - 6, {3100, 214.149994, 3200, 3200, 214.600006, 3200, 0, 158.5, 0, 100, 158.949997, 0}, "position");
    expectNumbers(*g.position(), 1221 * 3 - 15, {0, 159, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, "position");
    expectNumbers(*g.normal(), 0, {0.594777, 0.747676, 0.295332, -0.004185, 0.930063, 0.367375}, "normal");
    expectNumbers(*g.normal(), 33 * 33 * 3, {0.447214, 0, 0.894427}, "normal");
    expectNumbers(*g.index, 0, {0, 33, 1, 1, 33, 34, 1, 34, 2, 2, 34, 35}, "index", 0);
    expectNumbers(*g.index, 32 * 32 * 6, {0, 1089, 1, 1, 1089, 1090, 1, 1090, 2, 2, 1090, 1091}, "index", 0);
    expectNumbers(*g.index, 6912 - 6, {33, 1216, 0, 0, 1216, 1089}, "index", 0);
    // the colours of the cover, a little greyer than near by: the first row of this chunk has every class once (and two that are none: as meadow)
    expectNumbers(*g.color(), 0,
                  {0.222074, 0.336464, 0.129563, 0.390749, 0.374401, 0.1567,   0.18149,  0.336464, 0.119546, 0.085346, 0.166619, 0.092738, 0.142308, 0.238862, 0.106261,
                   0.212868, 0.3127,   0.14639,  0.325971, 0.336464, 0.315467, 0.283321, 0.286491, 0.279177, 0.325971, 0.275752, 0.189353, 0.218974, 0.316577, 0.133113,
                   0.251394, 0.361447, 0.136771, 0.18149,  0.293814, 0.14246,  0.203942, 0.194684, 0.189353, 0.334162, 0.279299, 0.205105, 0.142308, 0.336464, 0.14246,
                   0.142308, 0.155033, 0.172319, 0.363907, 0.357198, 0.334852, 0.106912, 0.194684, 0.282659, 0.222074, 0.336464, 0.129563, 0.222074, 0.336464, 0.129563},
                  "colour");
    expectNumbers(*g.color(), 33 * 33 * 3, {0.222074, 0.336464, 0.129563, 0.390749, 0.374401, 0.1567}, "colour");
    // where no ground is known a point lies at 0 (the chunk beside the hole)
    const BufferAttribute& p = *chunkAt(far, 3, 1).geometry->position();
    assert(near(p.getY(0), 237.350006, 1e-4) && p.getY(32 * 33) == 0 && p.getY(32 * 33 + 5) == 0);
  }
  // nothing is made anew while the camera stays
  std::vector<Object3D*> were;
  for (const ObjectPtr& o : far.group->children) were.push_back(o.get());
  far.update(1000, 500, 100);
  assert(far.group->children.size() == 26);
  for (size_t i = 0; i < were.size(); i++) assert(far.group->children[i].get() == were[i]);

  // another grid: everything goes; then two chunks a call, the nearest first
  far.set(madeUpGround());
  assert(far.group->children.empty() && far.ready());
  far.update(1000, 500);
  assert(chunks(far) == "1_0:1 1_1:1");
  far.update(1000, 500);
  assert(chunks(far) == "1_0:1 1_1:1 2_0:1 2_1:1");

  // what lies wholly inside the circle of the tiles is not made
  far.set(madeUpGround());
  far.inner = 6000;
  far.update(-2400, -1400, 100);
  assert(chunks(far) == "0_2:1 0_3:2 0_4:2 1_1:1 1_2:2 1_3:2 1_4:2 2_0:1 2_1:2 2_2:2 2_3:2 3_0:2 3_1:2 3_3:2 4_0:2 4_1:2");
  assert(std::get<double>(uInner.value) == 6000 && std::get<Vector2>(uCam.value).x == -2400 && std::get<Vector2>(uCam.value).y == -1400);
  far.inner = 1200;
  // from further away: coarser (every fourth point from 14 km on) – a chunk whose step changes is made anew
  far.radius = 30000;
  far.update(-12000, -1400, 100);
  assert(chunks(far) == "0_0:2 0_1:2 0_2:2 0_3:2 0_4:2 1_0:2 1_1:2 1_2:2 1_3:2 1_4:4 2_0:4 2_1:4 2_2:4 2_3:4 2_4:4 3_0:4 3_1:4 3_3:4 3_4:4 4_0:4 4_1:4 4_2:4 4_3:4 4_4:4 5_0:4 5_1:4 5_2:4 5_3:4 5_4:4 "
                        "6_0:4 6_1:4 6_2:4 6_3:4 6_4:4");
  far.update(-21000, -1400, 100);
  assert(chunks(far) == "0_0:4 0_1:4 0_2:4 0_3:4 0_4:4 1_0:4 1_1:4 1_2:4 1_3:4 1_4:4 2_0:4 2_1:4 2_2:4 2_3:4 2_4:4 3_0:4 3_1:4 3_3:4 3_4:4 4_0:4 4_1:4");
  // while it is not shown nothing is made and nothing dropped – the shader still learns where the camera is
  far.group->visible = false;
  far.update(1000, 500, 100);
  assert(far.group->children.size() == 21 && std::get<Vector2>(uCam.value).x == 1000 && std::get<double>(uInner.value) == 1200);
  far.group->visible = true;
  // out of reach: everything goes; so it does for a camera that is nowhere
  far.radius = 12000;
  far.update(-21000, -1400, 100);
  assert(far.group->children.empty());
  far.update(1000, 500, 100);
  assert(far.group->children.size() == 26);
  far.update(std::nan(""), 500, 100);
  assert(far.group->children.empty());

  // the season is taught to the one material
  int taught = 0;
  far.setSeason([&](Material& m) {
    assert(&m == &mat);
    taught++;
  });
  assert(taught == 1);
  // no grid any more: nothing is drawn
  far.update(1000, 500, 100);
  far.set(std::nullopt);
  assert(!far.ready() && far.group->children.empty());
  far.update(1000, 500, 100);
  assert(far.group->children.empty());
}

// ---------------------------------------------------------------------------------------------
// the measured details of walls

/** a tile with a building mesh's style bytes, the walls that have details, and details in four of its sixteen squares */
static UnpackedTile madeUpDetails(bool details, bool wide = false) {
  TileHeader header;
  header.v = 2;
  header.cx = 1000;
  header.cz = -500;
  header.yBase = 200;
  header.size = 500;
  header.terrainN = 2;
  header.terrainMin = 200;
  header.terrainScale = 0.04;
  header.coverN = 1;
  // (style, r, g, b per corner of the building mesh; the walls with details as pairs: first corner, number of corners)
  const std::vector<uint8_t> a8 = {0, 1, 2, 3, 0, 1, 2, 3, 5, 1, 2, 3, 5, 1, 2, 3, 6, 1, 2, 3, 6, 1, 2, 3, 8, 1, 2, 3, 8, 1, 2, 3, 16, 1, 2, 3, 9, 1, 2, 3};
  const std::vector<uint32_t> runs = {2, 4, 8, 5};
  SectionList sections = {{"hgt", SectionArray(std::vector<uint16_t>(4))}, {"cov", SectionArray(std::vector<uint8_t>(1))}, {"bldg_a8", SectionArray(a8)}};
  if (details) {
    const std::vector<int16_t> pos = {-12000, 100, -12000, -11000, 100, -12000, -12000, 600,  -11500, 100,  0, 100,   1100, 0, 100,   100, 900, 300,
                                      12400,  50,  -200,   12400,  50,  800,    12400,  1050, 300,    -100, 0, 12499, 900,  0, 12499, 400, 700, 12499};
    const std::vector<uint16_t> idx = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 3, 5, 4, 0, 2, 1, 4, 9, 6};
    std::vector<uint8_t> col(36);
    for (size_t i = 0; i < col.size(); i++) col[i] = static_cast<uint8_t>((i * 37 + 11) % 256);
    sections.push_back({"bldg_obs", SectionArray(runs)});
    sections.push_back({"fdet_pos", SectionArray(pos)});
    if (wide) sections.push_back({"fdet_idx", SectionArray(std::vector<uint32_t>(idx.begin(), idx.end()))});
    else sections.push_back({"fdet_idx", SectionArray(idx)});
    sections.push_back({"fdet_a8", SectionArray(col)});
  }
  return unpackTile(packTile(header, sections));
}

static void testFacadeDetail() {
  assert(OBSERVED == 16 && DETAIL_DISTANCE == 380);
  UnpackedTile data = madeUpDetails(true);
  // the game marks the walls once: a second time changes nothing
  std::vector<uint8_t>& a8 = data.get("bldg_a8")->u8;
  const SectionArray* runs = data.get("bldg_obs");
  assert(runs && runs->size() == 4 && a8.size() == 40);
  const std::vector<uint8_t> before = a8;
  assert(flagObservedWalls(&a8, runs) == 5);
  assert(flagObservedWalls(&a8, runs) == 0);
  const int styles[10] = {0, 0, 21, 21, 22, 22, 8, 8, 16, 25};
  for (size_t i = 0; i < 10; i++) {
    assert(a8[i * 4] == styles[i] && a8[i * 4] < 32);
    // (what the corners are painted with is left alone)
    assert(a8[i * 4 + 1] == before[i * 4 + 1] && a8[i * 4 + 2] == before[i * 4 + 2] && a8[i * 4 + 3] == before[i * 4 + 3]);
  }
  const SectionArray odd(std::vector<uint32_t>{1});
  assert(flagObservedWalls(nullptr, runs) == 0 && flagObservedWalls(&a8, nullptr) == 0 && flagObservedWalls(&a8, &odd) == 0);

  const MaterialPtr mat = makeDetailMaterial();
  assert(mat->type == MaterialType::Lambert && mat->vertexColors && mat->side == Side::Double && mat->polygonOffset && mat->polygonOffsetFactor == -1 && mat->polygonOffsetUnits == -2);

  // the details: one level of detail per square that has any, all triangles there
  const std::shared_ptr<Group> g = buildFacadeDetail(data, data.header, mat);
  assert(g && g->name == "fdet" && g->children.size() == 4 && !g->matrixAutoUpdate);
  const struct {
    double x, y, z;      // where the level of detail stands: the middle of its square
    double mx, mz;       // the mesh in it: the tile's frame
    std::vector<int> index;
    double sphere[4];
    double box[6];
  } squares[4] = {
      {812.5, 200, -687.5, 187.5, 187.5, {0, 1, 2, 0, 2, 1}, {-11500, 350, -11750, 612.372436}, {-12000, 100, -12000, -11000, 600, -11500}},
      {1187.5, 200, -562.5, -187.5, 62.5, {6, 7, 8}, {12400, 550, 300, 707.106781}, {12400, 50, -200, 12400, 1050, 800}},
      {1062.5, 200, -437.5, -62.5, -62.5, {3, 4, 5, 3, 5, 4, 4, 9, 6}, {6150, 450, 6149.5, 8920.826769}, {-100, 0, -200, 12400, 900, 12499}},
      {937.5, 200, -312.5, 62.5, -187.5, {9, 10, 11}, {400, 350, 12499, 610.327781}, {-100, 0, 12499, 900, 700, 12499}},
  };
  size_t triangles = 0;
  const BufferGeometry* shared = nullptr;
  for (size_t c = 0; c < 4; c++) {
    assert(g->children[c]->type == ObjectType::LOD);
    const LOD& lod = static_cast<const LOD&>(*g->children[c]);
    expectVector(lod.position, squares[c].x, squares[c].y, squares[c].z);
    assert(!lod.matrixAutoUpdate && lod.matrix.elements[12] == squares[c].x && lod.levels.size() == 2 && lod.levels[0].distance == 0 && lod.levels[1].distance == DETAIL_DISTANCE);
    // (beyond that distance: nothing)
    assert(lod.levels[1].object->type == ObjectType::Object3D && lod.levels[0].object->type == ObjectType::Mesh);
    const Mesh& mesh = static_cast<const Mesh&>(*lod.levels[0].object);
    assert(mesh.name == "fdet" && mesh.receiveShadow && !mesh.matrixAutoUpdate && mesh.material == mat);
    expectVector(mesh.position, squares[c].mx, 0, squares[c].mz);
    expectVector(mesh.scale, POS_XZ_SCALE, POS_Y_SCALE, POS_XZ_SCALE);
    assert(near(mesh.matrix.elements[0], 0.02) && near(mesh.matrix.elements[5], 0.01) && near(mesh.matrix.elements[12], squares[c].mx));
    const BufferGeometry& geo = *mesh.geometry;
    assert(geo.index && geo.index->type == AttrType::U16 && geo.index->count() == squares[c].index.size());
    for (size_t i = 0; i < squares[c].index.size(); i++) assert(geo.index->at(i) == squares[c].index[i]);
    triangles += geo.index->count() / 3;
    // (the sphere and the box around this square's corners only)
    assert(geo.boundingSphere && geo.boundingBox);
    expectSphere(*geo.boundingSphere, squares[c].sphere[0], squares[c].sphere[1], squares[c].sphere[2], squares[c].sphere[3], 2e-5);
    expectVector(geo.boundingBox->min, squares[c].box[0], squares[c].box[1], squares[c].box[2]);
    expectVector(geo.boundingBox->max, squares[c].box[3], squares[c].box[4], squares[c].box[5]);
    // the corners, their colours and their normals are one list for all the squares
    assert(geo.attributes.size() == 3 && geo.attributes[0].first == "position" && geo.attributes[1].first == "color" && geo.attributes[2].first == "normal");
    if (!shared) shared = &geo;
    assert(geo.position() == shared->position() && geo.color() == shared->color() && geo.normal() == shared->normal());
  }
  assert(triangles == 7);
  assert(shared->position()->type == AttrType::I16 && shared->position()->count() == 12 && shared->position()->getX(6) == 12400);
  assert(shared->color()->type == AttrType::U8 && shared->color()->normalized && shared->color()->itemSize == 3);
  assert(near(shared->color()->getX(1), 0.47843137254901963) && near(shared->color()->getZ(11), 0.10196078431372549));
  expectNumbers(*shared->normal(), 0, {0,         0,        0,         0,  0, 0, 0,  0, 0, 0,         0,       0,       -0.004436, 0.99999, -0.000429, 0, 0, 0,
                                       -0.011591, 0.999933, -0.000429, -1, 0, 0, -1, 0, 0, -0.004436, 0.99998, 0.00458, 0,         0,       1,         0, 0, 1},
                "normal");
  // (the arrays of the sections are those of the geometries now: there is nothing to build a second time)
  assert(data.get("fdet_pos")->size() == 0 && !buildFacadeDetail(data, data.header, mat));

  // a list of corners 32 bits wide stays so
  UnpackedTile wide = madeUpDetails(true, true);
  const std::shared_ptr<Group> g32 = buildFacadeDetail(wide, wide.header, mat);
  assert(g32 && g32->children.size() == 4);
  assert(static_cast<const Mesh&>(*static_cast<const LOD&>(*g32->children[0]).levels[0].object).geometry->index->type == AttrType::U32);

  // a tile without details: nothing
  UnpackedTile plain = madeUpDetails(false);
  assert(!buildFacadeDetail(plain, plain.header, mat));
  assert(flagObservedWalls(&plain.get("bldg_a8")->u8, plain.get("bldg_obs")) == 0);
}

// ---------------------------------------------------------------------------------------------
// the lights at night

static void testPlayerLights() {
  const std::shared_ptr<Scene> scene = make<Scene>();
  PlayerLights lights(scene);
  assert(lights.group->name == "headlights" && lights.group->children.size() == 4);
  // the pool of light of the low graphics lies in the scene from the start, unseen; the spotlights come when it first gets dark
  assert(scene->children.size() == 1 && scene->children[0]->type == ObjectType::Group && !scene->children[0]->visible);
  Object3D& frame = *scene->children[0];
  assert(frame.children.size() == 2);
  Mesh& poolLow = static_cast<Mesh&>(*frame.children[0]);
  Mesh& poolHigh = static_cast<Mesh&>(*frame.children[1]);
  for (const Mesh* m : {&poolLow, &poolHigh}) {
    assert(m->renderOrder == 2 && !m->frustumCulled && m->geometry->position()->count() == 63 && m->geometry->color()->count() == 63 && m->geometry->index->count() == 288);
    const Material& mat = *m->material;
    assert(mat.type == MaterialType::Basic && mat.vertexColors && mat.transparent && mat.blending == Blending::Additive && !mat.depthWrite && !mat.fog && !mat.toneMapped);
    assert(mat.polygonOffset && mat.polygonOffsetFactor == -2 && mat.polygonOffsetUnits == -4);
  }
  assert(poolLow.material != poolHigh.material);
  // a fan: narrow at the bumper, wide and to the right at its far end; brightest a little way ahead and in the middle
  expectNumbers(*poolLow.geometry->position(), 0, {-1.1, 0, -1, -0.733333, 0, -1, -0.366667, 0, -1}, "pool");
  expectNumbers(*poolLow.geometry->position(), 63 * 3 - 9, {4.033333, 0, -34, 5.866667, 0, -34, 7.7, 0, -34}, "pool");
  expectNumbers(*poolHigh.geometry->position(), 21, {-4.794113, 0, -11.75, -3.196075, 0, -11.75, -1.598037, 0, -11.75}, "pool");
  {
    const double low[9] = {0, 0.518431, 0.668476, 0.517883, 0.378929, 0.253305, 0.143587, 0.054409, 0};
    const double high[9] = {0, 0.460828, 0.594201, 0.460341, 0.336826, 0.22516, 0.127633, 0.048364, 0};
    for (size_t i = 0; i < 9; i++) {
      assert(near(poolLow.geometry->color()->getX(i * 7 + 3), low[i], 2e-6) && near(poolLow.geometry->color()->getZ(i * 7 + 3), low[i], 2e-6));
      assert(near(poolHigh.geometry->color()->getX(i * 7 + 2), high[i], 2e-6));
    }
  }
  expectNumbers(*poolLow.geometry->index, 0, {0, 7, 1, 1, 7, 8, 1, 8, 2, 2, 8, 9}, "pool index", 0);
  expectNumbers(*poolLow.geometry->index, 288 - 6, {54, 61, 55, 55, 61, 62}, "pool index", 0);
  // the dipped beam: warm, short, wide; the main beam: white, long, narrow – neither throws shadows
  assert(lights.group->children[0]->type == ObjectType::Light && lights.group->children[2]->type == ObjectType::Light);
  Light& dipped = static_cast<Light&>(*lights.group->children[0]);
  Light& main = static_cast<Light&>(*lights.group->children[2]);
  assert(dipped.lightType == LightType::Spot && dipped.color.getHex() == 0xfff1dc && dipped.intensity == 0 && dipped.distance == 95 && dipped.angle == 0.42 && dipped.penumbra == 0.85 && dipped.decay == 1.5);
  assert(main.lightType == LightType::Spot && main.color.getHex() == 0xffffff && main.intensity == 0 && main.distance == 240 && main.angle == 0.3 && main.penumbra == 0.6 && main.decay == 1.5);
  assert(!dipped.castShadow && !main.castShadow && lights.group->children[1] == dipped.target && lights.group->children[3] == main.target);

  const std::shared_ptr<Group> root = make<Group>();
  root->position.set(100, 5, -40);
  root->rotation.y = 0.6;
  const auto state = [&](double dark, bool low, bool high, bool cheap, bool driving = true) {
    PlayerLightState s;
    s.dark = dark;
    s.root = driving ? root.get() : nullptr;
    s.front = 2.1;
    s.height = 0.7;
    s.low = low;
    s.high = high;
    s.cheap = cheap;
    lights.update(s);
  };
  const auto inScene = [&] { return std::find(scene->children.begin(), scene->children.end(), ObjectPtr(lights.group)) != scene->children.end(); };
  // by day: nothing
  state(0, true, false, false);
  assert(!frame.visible && !inScene() && dipped.intensity == 0 && main.intensity == 0);
  // the low graphics at dusk: the pool on the road in front of the bumper, 6 cm above the ground
  state(0.5, true, false, true);
  assert(frame.visible && poolLow.visible && !poolHigh.visible && !inScene() && !frame.matrixAutoUpdate);
  assert(near(poolLow.material->color.r, 0.275) && near(poolHigh.material->color.r, 0.2) && near(poolLow.material->color.b, 0.275));
  {
    const double want[16] = {0.825336, 0, -0.564642, 0, 0, 1, 0, 0, 0.564642, 0, 0.825336, 0, 98.814251, 5.06, -41.733205, 1};
    for (size_t k = 0; k < 16; k++) assert(near(frame.matrix.elements[k], want[k], 2e-6));
  }
  state(1, false, true, true);
  assert(frame.visible && poolLow.visible && poolHigh.visible && near(poolLow.material->color.r, 0.55) && near(poolHigh.material->color.r, 0.4) && !inScene());
  // the spotlights instead (medium graphics and up): at the lamps, aimed down and a little to the right – the main beam level and far
  state(0.5, true, false, false);
  assert(!frame.visible && inScene() && near(dipped.intensity, 160) && main.intensity == 0);
  expectVector(dipped.position, 98.757787, 5.7, -41.815738);
  expectVector(dipped.target->position, 80.372301, 4.75, -71.523379);
  expectVector(main.position, 98.757787, 5.7, -41.815738);
  expectVector(main.target->position, 31.057154, 5.1, -140.773479);
  assert(near(dipped.target->matrixWorld.elements[12], 80.372301, 2e-6) && near(main.target->matrixWorld.elements[14], -140.773479, 2e-6));
  state(0.8, true, true, false);
  assert(near(dipped.intensity, 256) && near(main.intensity, 1360) && scene->children.size() == 2);
  // switched off, or nobody at the wheel: dark, and they stay in the scene
  state(0.8, false, false, false);
  assert(dipped.intensity == 0 && main.intensity == 0 && inScene());
  state(0.8, true, true, false);
  assert(near(dipped.intensity, 256));
  state(0.8, true, true, false, false);
  assert(dipped.intensity == 0 && main.intensity == 0 && inScene() && scene->children.size() == 2);
}

static void testTrafficGlow() {
  TrafficGlow glow;
  Points& points = *glow.points;
  assert(!points.visible && points.renderOrder == 5 && !points.frustumCulled && points.name == "traffic lamps");
  const BufferGeometry& geo = *points.geometry;
  const Material& mat = *points.material;
  assert(mat.type == MaterialType::Shader && mat.transparent && !mat.depthWrite && mat.blending == Blending::Additive && !mat.fog);
  assert(mat.vertexShader.find("gl_PointSize = max(2.0, aSize * uScale / max(0.5, -mv.z));") != std::string::npos);
  assert(mat.fragmentShader.find("float k = exp(-d * d * 7.0) + 0.6 * pow(1.0 - d, 6.0);") != std::string::npos);
  const BufferAttribute& pos = *geo.getAttribute("position");
  const BufferAttribute& col = *geo.getAttribute("color");
  const BufferAttribute& size = *geo.getAttribute("aSize");
  // room for the four lamps of 400 cars, written anew every frame
  assert(pos.count() == 1600 && col.count() == 1600 && size.count() == 1600 && size.itemSize == 1 && pos.dynamic && col.dynamic && size.dynamic);
  assert(geo.drawRange.start == 0 && geo.drawRange.count == 0);
  const std::shared_ptr<Camera> camera = perspectiveCamera(60, 16.0 / 9, 0.1, 2000);
  camera->position.set(0, 1.4, 0);
  const auto car = [](int id, double x, double y, double z, double yaw, double length, double width, bool braking) {
    GlowCar c;
    c.id = id;
    c.x = x;
    c.y = y;
    c.z = z;
    c.yaw = yaw;
    c.length = length;
    c.width = width;
    c.braking = braking;
    return c;
  };
  const std::vector<GlowCar> cars = {
      car(1, 0, 0.2, -50, PI, 4.5, 1.8, false),   // comes towards the camera
      car(2, 3, 0, -30, 0, 4.5, 1.8, true),       // drives away, braking
      car(3, -3, 0, -80, PI, 12, 2.5, false),     // a lorry, its main beam on
      car(4, 40, 0, 0, 0, 4.5, 1.8, false),       // seen from the side: neither its front nor its back
      car(5, 0, 0, -950, PI, 4.5, 1.8, false),    // too far
      car(6, 0, 0, 0, 0.3, 4.5, 1.8, false),      // where the camera is
  };
  const auto none = [](int) { return false; };
  // not dark enough yet: nothing
  glow.update(cars, 0.03, *camera, 720, none, 0);
  assert(!points.visible && geo.drawRange.count == 0);
  // at night: two white points in front of those that come, two red ones behind those that go – brighter braking, larger with the main beam
  const uint32_t version = pos.version;
  glow.update(cars, 0.8, *camera, 720, [](int id) { return id == 3; }, 0.25);
  assert(points.visible && geo.drawRange.start == 0 && geo.drawRange.count == 6);
  assert(near(std::get<double>(mat.uniforms.at("uScale")->value), 623.538291, 1e-6));
  expectNumbers(pos, 0, {0.62, 0.86, -47.630001, -0.62, 0.86, -47.630001, 2.38, 0.88, -27.67, 3.62, 0.88, -27.67, -2.03, 0.9, -73.879997, -3.97, 0.9, -73.879997}, "lamp");
  expectNumbers(col, 0, {0.88, 0.8448, 0.7568, 0.88, 0.8448, 0.7568, 1.267327, 0.07604, 0.050693, 1.267327, 0.07604, 0.050693, 2.356686, 2.262419, 2.02675, 2.356686, 2.262419, 2.02675}, "lamp colour");
  expectNumbers(size, 0, {0.75, 0.75, 0.7, 0.7, 2.85, 2.85}, "lamp size");
  assert(pos.version == version + 1 && col.version > 1 && size.version > 1);
  // no traffic: unseen (what was written stays)
  glow.update({}, 0.8, *camera, 720, none, 0);
  assert(!points.visible && geo.drawRange.count == 6);
  // more lamps than there is room for: the first 1600
  std::vector<GlowCar> many;
  for (int i = 0; i < 900; i++) many.push_back(car(i, (i % 30) * 3 - 45, 0, -20 - (i / 30) * 8, PI, 4.5, 1.8, false));
  glow.update(many, 1, *camera, 1080, none, 0);
  assert(points.visible && geo.drawRange.count == 1600);
}

int main() {
  testFar();
  testFacadeDetail();
  testPlayerLights();
  testTrafficGlow();
  std::printf("render_far_test: ok\n");
  return 0;
}
