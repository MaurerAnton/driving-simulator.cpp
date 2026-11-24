// SPDX-License-Identifier: AGPL-3.0-only
// LINK: render engine world sim core platform geo
/**
 * The traffic signs (render/signs, render/signart) and the pedestrian lights (render/pedlights):
 * which signs the program draws itself and how wide their plates are (what
 * tests/game/learn.test.ts asks of artAspect), what is written on them, the sizes of the plates,
 * the layout of the atlas – which cell a sign gets, when a cell is given again –, the plates and
 * posts of a group, and the instances of the lights.  The numbers are the original's: v1.7.7 run
 * on the same signs.  (No pixel is looked at: the native host paints nothing.)
 */
#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "engine/engine.h"
#include "platform/canvas.h"
#include "platform/native/canvas_native.h"
#include "platform/native/native.h"
#include "render/pedlights.h"
#include "render/signart.h"
#include "render/signs.h"
#include "render/tiles.h"
#include "sim/signals.h"
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

static void expectSphere(const Sphere& s, double x, double y, double z, double r, double eps = 2e-6) {
  if (!(near(s.center.x, x, eps) && near(s.center.y, y, eps) && near(s.center.z, z, eps) && near(s.radius, r, eps)))
    std::printf("sphere %.6f %.6f %.6f %.6f, not %.6f %.6f %.6f %.6f\n", s.center.x, s.center.y, s.center.z, s.radius, x, y, z, r);
  assert(near(s.center.x, x, eps) && near(s.center.y, y, eps) && near(s.center.z, z, eps) && near(s.radius, r, eps));
}

static SignPost post(double x, double y, double z, double yaw, std::vector<std::string> s, double h, int big = 0) {
  SignPost p;
  p.x = x;
  p.y = y;
  p.z = z;
  p.yaw = yaw;
  p.s = std::move(s);
  p.h = h;
  p.big = big;
  return p;
}

/** the plates of a group made by SignRenderer::build, and its posts */
static Mesh& platesOf(const std::shared_ptr<Group>& group) {
  assert(group->children.size() == 2 && group->children[1]->name == "signs" && group->children[1]->type == ObjectType::Mesh);
  return static_cast<Mesh&>(*group->children[1]);
}
static InstancedMesh& postsOf(const std::shared_ptr<Group>& group) {
  assert(group->children[0]->type == ObjectType::InstancedMesh);
  return static_cast<InstancedMesh&>(*group->children[0]);
}
/** the place in the atlas of plate `k` of a group (the uv of its front: four corners) */
static void expectCell(const std::shared_ptr<Group>& group, size_t k, double u0, double v0, double u1, double v1) {
  expectNumbers(*platesOf(group).geometry->uv(), k * 16, {u0, v0, u1, v0, u1, v1, u0, v1}, "uv");
}

// ---------------------------------------------------------------------------------------------

static void testCodes() {
  assert(splitCode("1040-30:22:00-06:00") == (std::pair<std::string, std::string>("1040-30", "22:00-06:00")));
  assert(splitCode("205") == (std::pair<std::string, std::string>("205", "")));
  assert(splitCode("310:Baunatal|Stadt") == (std::pair<std::string, std::string>("310", "Baunatal|Stadt")));

  // the width / height of the plates signart draws; none (-1 here) for what it does not draw
  const double P2 = 600.0 / 330, P3 = 600.0 / 450, T = 847.0 / 744;
  const struct {
    const char* code;
    double aspect;
  } plates[] = {
      {"1040-30:22:00-06:00", P2}, {"1042-33:Mo-Fr 07:00-17:00", P2}, {"1040-35:22-6", P2}, {"1040-36", P2}, {"1042-53:x", P2}, {"1040-30", -1},
      {"1026-36", P3}, {"1053-53", P3}, {"1007-33", P2}, {"1006-31", P2}, {"1004-30:150", P2}, {"1004-30", P2}, {"1001-31:2", P2}, {"1012-34", P2},
      {"1020-32:7", P3}, {"1010-50", P2}, {"1010-65", P2}, {"1020-11", P3}, {"1048-20", P2}, {"1049-13", P3}, {"1046-11", P2}, {"256", 1},
      {"257-54", 1}, {"133-20", T}, {"140-10", T}, {"108-12", T}, {"110-8", T}, {"117-10", T}, {"238", 1}, {"262:7.5", 1}, {"263", 1},
      {"264:2.3", 1}, {"265:3.8", 1}, {"266:10", 1}, {"268", 1}, {"273", 1}, {"275", 1}, {"275-30", 1}, {"279-30", 1}, {"277.1", 1},
      {"270.1", 0.8}, {"270.2", 0.8}, {"244.3", 1}, {"244.4", 1}, {"244.1", -1}, {"314.1", 1}, {"314.2", 1}, {"357-50", 1}, {"357-52", 1},
      {"354", 1}, {"356", 1}, {"358", 1}, {"363", 1}, {"365-50", 1}, {"454-10", 2}, {"454-20", 2}, {"455.1-11", 1.6}, {"455.1-123", 1.6},
      {"457.1", 1.6}, {"457.2", 1.6}, {"605-10", 0.25}, {"605-21", 0.25}, {"626-10", 4}, {"600", 4}, {"101", -1}, {"205", -1}, {"999", -1},
      {"", -1}, {"274-50", -1}, {"1020-30", -1}, {"108", -1}, {"275-x", -1}, {"357-53", -1},
  };
  for (const auto& p : plates) {
    const std::optional<double> a = artAspect(p.code);
    if (p.aspect < 0 ? a.has_value() : !a || !near(*a, p.aspect, 1e-12)) std::printf("artAspect(%s): %f, not %f\n", p.code, a.value_or(-1), p.aspect);
    assert(p.aspect < 0 ? !a : a && near(*a, p.aspect, 1e-12));
  }
  // (tests/game/learn.test.ts: every sign of the trainer that has no graphic can be drawn – game/exam/signs.ts ART_SIGNS)
  for (const char* c : {"133-20", "136-20", "138-20", "140-10", "142-20", "144-10", "238",  "257-53", "257-54", "257-58",  "258",     "261",     "263",     "266",    "268",
                        "269",    "270.1",  "270.2",  "273",    "275-30", "277.1",  "279-30", "354",  "358",    "363",     "1010-62", "1022-12", "1024-14", "1024-17"})
    assert(artAspect(c).has_value());
}

/** what `drawSignArt` writes on a plate */
static std::vector<std::string> written(const Ctx2D& g, const std::string& code, bool known = true) {
  canvasNative::forget(g);
  const bool drawn = drawSignArt(g, code, 10, 20, 182, 100);
  if (drawn != known) std::printf("drawSignArt(%s): %d\n", code.c_str(), drawn);
  assert(drawn == known);
  // (a sign that is not known leaves the canvas as it was: the state kept, and put back)
  assert(known ? canvasNative::calls(g) > 4 : canvasNative::calls(g) == 2);
  return canvasNative::texts(g);
}

static void testDrawing() {
  using Texts = std::vector<std::string>;
  const Canvas canvas(400, 300);
  const Ctx2D g = canvas.getContext2D();
  assert(written(g, "262:7.5") == Texts{"7,5 t"});
  assert(written(g, "1004-30:150") == Texts{"150 m"});
  assert(written(g, "1004-30") == Texts{"100 m"});
  assert(written(g, "1001-31:2") == Texts{"auf 2 km"});
  assert(written(g, "1012-34") == (Texts{"Grüne Welle", "bei 50 km/h"}));
  assert(written(g, "270.1") == (Texts{"Umwelt", "ZONE"}));
  assert(written(g, "108-12") == Texts{"12 %"});
  assert(written(g, "275-30") == Texts{"30"});
  assert(written(g, "279") == Texts{"30"});
  assert(written(g, "273") == Texts{"70 m"});
  assert(written(g, "1048-20") == (Texts{"nur", "und Lkw"}));
  assert(written(g, "1020-11") == (Texts{"mit Parkausweis", "Nr. … frei"}));
  assert(written(g, "1010-72") == Texts{"+ Pkw"});
  assert(written(g, "455.1-11") == Texts{"Umleitung"});
  assert(written(g, "457.1") == (Texts{"Umleitung", "→"}));
  assert(written(g, "457.2") == (Texts{"Ende der", "Umleitung"}));
  assert(written(g, "454-10") == Texts{"U"});
  assert(written(g, "1028-33:Hof") == (Texts{"Zufahrt bis", "Hof", "frei"}));
  assert(written(g, "363") == Texts{"POLIZEI"});
  assert(written(g, "314.1") == (Texts{"P", "ZONE"}));
  assert(written(g, "266:10") == Texts{"10 m"});
  assert(written(g, "605-10").empty());
  assert(written(g, "999", false).empty());
  assert(written(g, "101", false).empty());

  // the time on a panel: read and written by sim/signtime where that is there – as the map writes it where not
  assert(!parseTimeWindow && !windowLines);
  assert(written(g, "1040-30:22:00-06:00") == Texts{"22:00-06:00"});
  assert(written(g, "1040-35:22-6") == (Texts{"Lärmschutz", "22-6"}));
  std::vector<std::string> asked;
  parseTimeWindow = [&](const std::string& raw) -> std::optional<TimeWindow> {
    asked.push_back(raw);
    if (raw == "tomorrow") return std::nullopt;
    TimeWindow w;
    w.spans.push_back({0x3e, 420, 1020});
    return w;
  };
  windowLines = [](const TimeWindow& w) -> std::vector<std::string> {
    assert(w.spans.size() == 1 && w.spans[0].days == 0x3e && w.spans[0].from == 420 && w.spans[0].to == 1020 && w.off == 0);
    return {"Mo–Fr", "7–17 h"};
  };
  assert(written(g, "1042-33:Mo-Fr 07:00-17:00") == (Texts{"Mo–Fr", "7–17 h"}));
  assert(asked == Texts{"Mo-Fr 07:00-17:00"});
  assert(written(g, "1040-36:Mo-Fr 07:00-17:00") == (Texts{"Schulweg", "Mo–Fr", "7–17 h"}));
  assert(near(*artAspect("1040-36:Mo-Fr 07:00-17:00"), 600.0 / 450) && near(*artAspect("1042-33:Mo-Fr 07:00-17:00"), 600.0 / 330));
  // (a time that is not understood stays as it is written)
  assert(written(g, "1040-30:tomorrow") == Texts{"tomorrow"});
  // (a panel without a time asks nothing)
  asked.clear();
  assert(written(g, "1040-36") == Texts{"Schulweg"} && written(g, "1004-30:150") == Texts{"150 m"} && asked.empty());
  parseTimeWindow = nullptr;
  windowLines = nullptr;
}

static void testSizes() {
  const struct {
    const char* code;
    double aspect;
    bool big;
    double w, h;
  } sizes[] = {
      {"205", 1.14, false, 0.9, 0.7894736842105264},
      {"310:Baunatal", 1.5, false, 1.2, 0.8},
      {"311:x", 1.5, true, 1.74, 1.16},
      {"1020-30", 1.82, false, 0.6, 0.32967032967032966},
      {"1020-30", 1.82, true, 0.87, 0.478021978021978},
      {"605-10", 0.25, false, 0.25, 1},
      {"159-10", 0.3, false, 0.3, 1},
      {"162-20", 0, false, 0.3, 1},
      {"156-10", 0, false, 0.6, 0.6},
      {"156-20", 0.75, false, 0.6, 0.7999999999999999},
      {"626-10", 4, false, 1, 0.25},
      {"600", 4, false, 1, 0.25},
      {"454-10", 2, false, 0.9, 0.45},
      {"455.1-11", 1.6, false, 0.9, 0.5625},
      {"457.1", 1.6, false, 0.9, 0.56},
      {"457.2", 1.6, false, 0.9, 0.5625},
      {"274-50", 1, false, 0.6, 0.6},
      {"101", 1.1385, false, 0.9, 0.7905138339920948},
      {"206", 1, false, 0.9, 0.9},
      {"306", 1, false, 0.84, 0.84},
      {"325.1", 1.5, false, 0.9, 0.6},
      {"331.1", 1, false, 0.9, 0.9},
      {"201-50", 1.5, false, 1, 0.6666666666666666},
      {"350", 1, false, 0.6, 0.6},
      {"x", 1, false, 0.6, 0.6},
      {"101", std::nan(""), true, 1.305, 1.305},
  };
  for (const auto& s : sizes) {
    const std::pair<double, double> size = signSize(s.code, s.aspect, s.big);
    if (!(near(size.first, s.w, 1e-12) && near(size.second, s.h, 1e-12))) std::printf("signSize(%s): %f × %f, not %f × %f\n", s.code, size.first, size.second, s.w, s.h);
    assert(near(size.first, s.w, 1e-12) && near(size.second, s.h, 1e-12));
  }
}

static void testAtlas() {
  SignRenderer signs("signs/", 16);
  assert(signs.texture && signs.texture->width == 1920 && signs.texture->height == 1920 && signs.texture->anisotropy == 8 && signs.texture->srgb);
  assert(signs.texture->generateMipmaps && signs.texture->minFilter == Filter::LinearMipmapLinear);
  assert(signs.material->type == MaterialType::Basic && signs.material->map == signs.texture && signs.material->alphaTest == 0.45 && !signs.material->transparent && !signs.material->patch);
  assert(signs.backMaterial->map == signs.texture && signs.backMaterial->alphaTest == 0.45 && signs.backMaterial->patch && signs.backMaterial->patch->key == "fs-sign-back");
  assert(signs.backMaterial->patch->fragmentSource.find("diffuseColor.rgb = vec3(0.30, 0.32, 0.33);") != std::string::npos);

  // three posts: a give-way sign with a panel beneath, a large town sign, a speed limit above a give-way sign and a beacon
  const std::vector<SignPost> posts = {
      post(1010, 201, -520, 0, {"205", "1020-30"}, 2),
      post(990, 202.5, -480, PI / 2, {"310:Baunatal|Stadt"}, 1.2, 1),
      post(1000, 200, -500, -0.7, {"274-50", "205", "605-10"}, 2.2),
  };
  const uint32_t before = signs.texture->version;
  const std::shared_ptr<Group> a = signs.build(posts, 1000, 200, -500);
  Mesh& plates = platesOf(a);
  InstancedMesh& poles = postsOf(a);
  assert(plates.materials.size() == 2 && plates.materials[0] == signs.material && plates.materials[1] == signs.backMaterial && !plates.material);
  assert(plates.position.x == 1000 && plates.position.y == 200 && plates.position.z == -500 && !plates.matrixAutoUpdate && plates.matrix.elements[12] == 1000);
  const BufferGeometry& geo = *plates.geometry;
  // six plates: each a front and a back of four corners, about the group's middle
  assert(geo.position()->count() == 48 && geo.uv()->count() == 48 && geo.index->count() == 72 && geo.index->type == AttrType::U16);
  expectNumbers(*geo.position(), 0,
                {10.3,      3,        -20.035,   9.7,       3,        -20.035,   9.7,       3.32967,  -20.035,   10.3,      3.32967,  -20.035,   10.3,      3,        -19.993,   9.7,
                 3,         -19.993,  9.7,       3.32967,   -19.993,  10.3,      3.32967,   -19.993,  10.45,     3.35967,   -20.035,  9.55,      3.35967,   -20.035,  9.55,      4.149144,
                 -20.035,   10.45,    4.149144,  -20.035,   10.45,    3.35967,   -19.993,   9.55,     3.35967,   -19.993,   9.55,     4.149144,  -19.993,   10.45,    4.149144,  -19.993,
                 -10.035,   3.7,      19.129999, -10.035,   3.7,      20.870001, -10.035,   4.86,     20.870001, -10.035,   4.86,     19.129999, -9.993,    3.7,      19.129999, -9.993,
                 3.7,       20.870001, -9.993,   4.86,      20.870001, -9.993,   4.86,      19.129999, 0.118153, 2.2,       0.053758, -0.073058, 2.2,       -0.107297, -0.073058, 3.2,
                 -0.107297, 0.118153, 3.2,       0.053758,  0.091096, 2.2,       0.085881,  -0.100115, 2.2,      -0.075173, -0.100115, 3.2,      -0.075173, 0.091096, 3.2,       0.085881,
                 0.366727,  3.23,     0.263128,  -0.321631, 3.23,     -0.316667, -0.321631, 4.019474, -0.316667, 0.366727,  4.019474, 0.263128,  0.339669,  3.23,     0.295252,  -0.348689,
                 3.23,      -0.284544, -0.348689, 4.019474, -0.284544, 0.339669, 4.019474,  0.295252, 0.252,     4.049474,  0.166496, -0.206905, 4.049474,  -0.220035, -0.206905, 4.649474,
                 -0.220035, 0.252,    4.649474,  0.166496,  0.224943, 4.049474,  0.198619,  -0.233962, 4.049474, -0.187911, -0.233962, 4.649474, -0.187911, 0.224943, 4.649474,  0.198619},
                "position");
  // the atlas: cell 0 is the bare back; the panel (wide) takes cells 1 and 2, the give-way sign 3, the town sign 4 and 5, the beacon 6, the limit 7
  expectCell(a, 0, 0.11374, 0.902604, 0.28626, 0.997396);
  expectCell(a, 1, 0.302604, 0.908425, 0.397396, 0.991575);
  expectCell(a, 2, 0.428906, 0.902604, 0.571094, 0.997396);
  expectCell(a, 3, 0.638151, 0.902604, 0.661849, 0.997396);
  expectCell(a, 4, 0.302604, 0.908425, 0.397396, 0.991575);
  expectCell(a, 5, 0.702604, 0.902604, 0.797396, 0.997396);
  // (the back of a plate shows the same place of the atlas: its outline)
  for (size_t k = 0; k < 6; k++)
    for (size_t i = 0; i < 8; i++) assert(geo.uv()->at(k * 16 + i) == geo.uv()->at(k * 16 + 8 + i));
  // the fronts first, then the backs the other way round – a group of the geometry each, for the two materials
  {
    const int idx[72] = {0,  1,  2,  0,  2,  3,  8,  9,  10, 8,  10, 11, 16, 17, 18, 16, 18, 19, 24, 25, 26, 24, 26, 27, 32, 33, 34, 32, 34, 35, 40, 41, 42, 40, 42, 43,
                         4,  6,  5,  4,  7,  6,  12, 14, 13, 12, 15, 14, 20, 22, 21, 20, 23, 22, 28, 30, 29, 28, 31, 30, 36, 38, 37, 36, 39, 38, 44, 46, 45, 44, 47, 46};
    for (size_t i = 0; i < 72; i++) assert(geo.index->at(i) == idx[i]);
    assert(geo.groups.size() == 2 && geo.groups[0].start == 0 && geo.groups[0].count == 36 && geo.groups[0].materialIndex == 0);
    assert(geo.groups[1].start == 36 && geo.groups[1].count == 36 && geo.groups[1].materialIndex == 1);
  }
  assert(geo.boundingSphere);
  expectSphere(*geo.boundingSphere, 0.2075, 3.53, 0.4175, 22.912496, 2e-5);
  // the posts: from 30 cm in the ground to the top of the highest plate
  assert(poles.count == 3 && poles.geometry->position()->count() == 14 && poles.material->type == MaterialType::Lambert && poles.boundingSphere);
  {
    const double want[3][16] = {
        {1, 0, 0, 0, 0, 3.479144, 0, 0, 0, 0, 1, 0, 1010, 200.699997, -520, 1},
        {1, 0, 0, 0, 0, 2.69, 0, 0, 0, 0, 1, 0, 990, 202.199997, -480, 1},
        {1, 0, 0, 0, 0, 4.979474, 0, 0, 0, 0, 1, 0, 1000, 199.699997, -500, 1},
    };
    Matrix4 m4;
    for (int i = 0; i < 3; i++) {
      poles.getMatrixAt(i, m4);
      for (size_t k = 0; k < 16; k++) assert(near(m4.elements[k], want[i][k], 2e-6 * (1 + std::fabs(want[i][k]))));
    }
  }
  // (no graphics were read: these three are drawn by the simple fallback – the town sign and the beacon are drawn as they are meant to be)
  assert(signs.missing == (std::vector<std::string>{"1020-30", "205", "274-50"}));
  // the atlas goes to the graphics card once after signs were drawn
  assert(signs.texture->version == before);
  signs.flush();
  assert(signs.texture->version == before + 1);
  signs.flush();
  assert(signs.texture->version == before + 1);

  // the same sign in another group: the same cell, nothing drawn anew
  const std::shared_ptr<Group> b = signs.build({post(0, 0, 0, 0, {"205"}, 2)}, 0, 0, 0);
  expectCell(b, 0, 0.302604, 0.908425, 0.397396, 0.991575);
  signs.flush();
  assert(signs.texture->version == before + 1);
  signs.release(*a);
  signs.release(*a);   // (twice: nothing more happens)

  // the graphics and their sizes, as init() reads them
  namespace fsys = std::filesystem;
  const fsys::path dir = fsys::temp_directory_path() / "fs_render_signs_test";
  fsys::create_directories(dir);
  std::ofstream(dir / "signs.json") << R"({"101": "<svg viewBox=\"0 0 847 744\"></svg>", "133-10": "<svg width=\"847\" height=\"744\"></svg>", "274": "<svg/>", "350": "<svg/>", "1048-12": "<svg/>", "262": "<svg/>", "7": 7})";
  std::ofstream(dir / "manifest.json")
      << R"({"101": {"width": 847, "height": 744, "shape": "triangle"}, "133-10": {"width": 847.15857, "height": 743.62268}, "350": {"width": 600, "height": 600}, "1048-12": {"width": 600, "height": 330}, "262": {"width": 600, "height": 600}})";
  plat::native::mount("signs/", dir.string());
  plat::native::wait(signs.init());
  // a graphic of its own (262, 101), one mirrored (133-20 from 133-10): in the free cells 8, 9 and 10, shaped as the graphics are
  const std::shared_ptr<Group> c = signs.build({post(0, 0, 0, 0, {"101", "133-20", "262"}, 2)}, 0, 0, 0);
  expectCell(c, 0, 0.802604, 0.902604, 0.897396, 0.997396);
  expectCell(c, 1, 0.902604, 0.908397, 0.997396, 0.991603);
  expectCell(c, 2, 0.002604, 0.808368, 0.097396, 0.891632);
  expectNumbers(*platesOf(c).geometry->position(), 0,
                {0.3,  2,    -0.035, -0.3,  2,        -0.035, -0.3,  2.6,      -0.035, 0.3,   2.6,      -0.035, 0.3,  2,        0.007,  -0.3,  2,        0.007,
                 -0.3, 2.6,  0.007,  0.3,   2.6,      0.007,  0.45,  2.63,     -0.035, -0.45, 2.63,     -0.035, -0.45, 3.420006, -0.035, 0.45,  3.420006, -0.035,
                 0.45, 2.63, 0.007,  -0.45, 2.63,     0.007,  -0.45, 3.420006, 0.007,  0.45,  3.420006, 0.007,  0.45, 3.450006, -0.035, -0.45, 3.450006, -0.035,
                 -0.45, 4.240561, -0.035, 0.45, 4.240561, -0.035, 0.45, 3.450006, 0.007, -0.45, 3.450006, 0.007, -0.45, 4.240561, 0.007, 0.45, 4.240561, 0.007},
                "position");
  expectSphere(*platesOf(c).geometry->boundingSphere, 0, 3.120281, -0.014, 1.207464, 2e-5);
  {
    Matrix4 m4;
    postsOf(c).getMatrixAt(0, m4);
    assert(near(m4.elements[5], 4.570561, 1e-5) && near(m4.elements[13], -0.3, 1e-6));
  }
  // (the native host decodes no pictures: the graphics count as missing, and the signs are drawn by the program instead)
  assert(signs.missing == (std::vector<std::string>{"1020-30", "205", "274-50", "262", "133-20", "101"}));
  // how a sign is drawn shows in the shape of its plate: a variant as the sign it belongs to (101-12), the graphic of another number
  // (350-10, 1010-51), a value on the plate drawn by the program (262:7.5), a speed limit never as its family's graphic (274-12)
  {
    const std::shared_ptr<Group> d = signs.build({post(0, 0, 0, 0, {"274-12", "262:7.5", "1010-51", "350-10", "101-12"}, 0)}, 0, 0, 0);
    const BufferAttribute& p = *platesOf(d).geometry->position();
    double y = 0;
    const double heights[5] = {0.9 / (847.0 / 744), 0.6, 0.6 / (600.0 / 330), 0.6, 0.6};
    const double widths[5] = {0.9, 0.6, 0.6, 0.6, 0.6};
    for (size_t k = 0; k < 5; k++) {
      assert(near(p.getY(k * 8), y, 1e-6) && near(p.getY(k * 8 + 2) - p.getY(k * 8), heights[k], 1e-6) && near(p.getX(k * 8) - p.getX(k * 8 + 1), widths[k], 1e-6));
      y += heights[k] + 0.03;
    }
    signs.release(*d);
  }

  // a tile: its posts, as `editPost` leaves them
  {
    TileHeader header;
    header.v = 2;
    header.cx = 1000;
    header.cz = -500;
    header.yBase = 200;
    header.size = 500;
    header.terrainN = 2;
    header.coverN = 1;
    header.signs = posts;
    LoadedTile tile;
    tile.key = "3_-2";
    tile.data = unpackTile(packTile(header, {{"hgt", SectionArray(std::vector<uint16_t>(4))}, {"cov", SectionArray(std::vector<uint8_t>(1))}}));
    tile.header = tile.data.header;
    tile.group = make<Group>();
    assert(tile.header.signs.size() == 3 && tile.header.signs[1].big == 1 && tile.header.signs[2].s.size() == 3);
    signs.add(tile);
    assert(tile.group->children.size() == 1);
    {
      const std::shared_ptr<Group> made = std::static_pointer_cast<Group>(tile.group->children[0]);
      assert(postsOf(made).count == 3 && platesOf(made).geometry->position()->count() == 48);
      expectCell(made, 1, 0.302604, 0.908425, 0.397396, 0.991575);
    }
    signs.remove(tile);
    signs.remove(tile);
    // (the level crossings draw some signs themselves: a post may go, or lose plates)
    LoadedTile edited;
    edited.key = "4_-2";
    edited.data = unpackTile(packTile(header, {{"hgt", SectionArray(std::vector<uint16_t>(4))}, {"cov", SectionArray(std::vector<uint8_t>(1))}}));
    edited.header = edited.data.header;
    edited.group = make<Group>();
    signs.editPost = [](const SignPost& p) -> std::optional<SignPost> {
      if (p.s.size() == 1) return std::nullopt;
      if (p.s.size() == 2) return p;
      SignPost q = p;
      q.s = {"205"};
      return q;
    };
    signs.add(edited);
    assert(edited.group->children.size() == 1);
    {
      const std::shared_ptr<Group> made = std::static_pointer_cast<Group>(edited.group->children[0]);
      assert(postsOf(made).count == 2 && platesOf(made).geometry->position()->count() == 24);
    }
    signs.remove(edited);
    // nothing left of any post, or a tile without signs: nothing is added
    LoadedTile bare;
    bare.key = "5_-2";
    bare.data = unpackTile(packTile(header, {{"hgt", SectionArray(std::vector<uint16_t>(4))}, {"cov", SectionArray(std::vector<uint8_t>(1))}}));
    bare.header = bare.data.header;
    bare.group = make<Group>();
    signs.editPost = [](const SignPost&) -> std::optional<SignPost> { return std::nullopt; };
    signs.add(bare);
    assert(bare.group->children.empty());
    signs.editPost = nullptr;
    bare.header.signs.clear();
    signs.add(bare);
    assert(bare.group->children.empty());
    signs.remove(bare);
  }
  plat::native::unmountAll();
  std::error_code ignored;
  fsys::remove(dir / "signs.json", ignored);
  fsys::remove(dir / "manifest.json", ignored);
  fsys::remove(dir, ignored);
}

static void testAtlasFull() {
  // the atlas fills up: 99 cells (cell 0 is the back) – then cells are given again, those whose sign went longest ago first
  SignRenderer signs("signs/", 1);
  assert(signs.texture->anisotropy == 1);
  const std::shared_ptr<Group> g1 = signs.build({post(0, 0, 0, 0, {"274-10"}, 2)}, 0, 0, 0);
  const std::shared_ptr<Group> g2 = signs.build({post(0, 0, 0, 0, {"274-20", "1020-30"}, 2)}, 0, 0, 0);
  std::vector<std::string> many;
  for (int i = 0; i < 96; i++) many.push_back("278-" + std::to_string(i));
  // (cells 5 to 99 for 95 of them: for the last one drawn – the top plate – none is left)
  const std::shared_ptr<Group> g3 = signs.build({post(0, 0, 0, 0, many, 2)}, 0, 0, 0);
  expectCell(g1, 0, 0.102604, 0.902604, 0.197396, 0.997396);
  expectCell(g2, 0, 0.21374, 0.902604, 0.38626, 0.997396);
  expectCell(g2, 1, 0.402604, 0.902604, 0.497396, 0.997396);
  expectCell(g3, 0, 0.502604, 0.902604, 0.597396, 0.997396);
  expectCell(g3, 94, 0.902604, 0.002604, 0.997396, 0.097396);
  expectCell(g3, 95, 0.01, 0.01, 0.02, 0.02);
  // every sign in use: a new one shows the bare back rather than another's picture
  const std::shared_ptr<Group> full = signs.build({post(0, 0, 0, 0, {"274-70"}, 2)}, 0, 0, 0);
  expectCell(full, 0, 0.01, 0.01, 0.02, 0.02);
  // two groups go; then: the limit of 90 gets the first cell of the panel that went first (2), the new panel – wide – cell 3 and the one of
  // the limit that went with it (4), the limit of 80 the cell of the group that went last (1); the one that found the atlas full stays
  // without while a group shows it
  signs.release(*g2);
  signs.release(*g1);
  const std::shared_ptr<Group> again = signs.build({post(0, 0, 0, 0, {"274-70", "274-80", "1020-31", "274-90"}, 2)}, 0, 0, 0);
  expectCell(again, 0, 0.202604, 0.902604, 0.297396, 0.997396);
  expectCell(again, 1, 0.313826, 0.902604, 0.486174, 0.997396);
  expectCell(again, 2, 0.102604, 0.902604, 0.197396, 0.997396);
  expectCell(again, 3, 0.01, 0.01, 0.02, 0.02);
  // … and gets a cell once nothing shows it any more and it is asked for anew
  signs.release(*full);
  signs.release(*again);
  const std::shared_ptr<Group> late = signs.build({post(0, 0, 0, 0, {"274-70"}, 2)}, 0, 0, 0);
  assert(platesOf(late).geometry->uv()->at(0) > 0.05);
}

static void testPedLights() {
  PedLightRenderer ped;
  assert(ped.group->name == "pedestrian lights" && ped.group->children.empty());
  const Sparse<SignalControllerJson> none;
  SignalSystem signals(none);
  ped.update(signals);   // (no lights yet: nothing to do)
  const auto pole = [](int controller, int group, double x, double y, double z, double yaw, double boxYaw) {
    PedPole p;
    p.controller = controller;
    p.group = group;
    p.x = x;
    p.y = y;
    p.z = z;
    p.yaw = yaw;
    p.boxYaw = boxYaw;
    return p;
  };
  const std::vector<PedPole> poles = {
      pole(3, 1, 100, 200, -50, 0.5, 2.0707963267948966),
      pole(3, 1, 108, 200.1, -52, 0.5 + PI, -1.0707963267948966),
      pole(7, 0, 300, 210, 40, -1, 0.5707963267948966),
  };
  ped.rebuild(poles);
  // the pole with the head, the box with the button, the red and the green figure, the lamp "Signal kommt": an instance a pole each
  assert(ped.group->children.size() == 5);
  std::vector<InstancedMesh*> meshes;
  for (const ObjectPtr& o : ped.group->children) {
    assert(o->type == ObjectType::InstancedMesh);
    meshes.push_back(static_cast<InstancedMesh*>(o.get()));
  }
  const size_t corners[5] = {84, 180, 4, 4, 4};
  const double spheres[5][4] = {{0, 1.3, 0.0425, 1.303287}, {0, 1.05, -0.0975, 0.156305}, {0, 2.4, -0.039, 0.141421}, {0, 2.1, -0.039, 0.141421}, {0, 1.12, -0.144, 0.0534}};
  // (the head and the figures face across the road, the box and its lamp along the kerb)
  const double head[16] = {-0.877583, 0, 0.479426, 0, 0, 1, 0, 0, -0.479426, 0, -0.877583, 0, 108, 200.100006, -52, 1};
  const double box[16] = {0.479426, 0, 0.877583, 0, 0, 1, 0, 0, -0.877583, 0, 0.479426, 0, 108, 200.100006, -52, 1};
  Matrix4 m4;
  Color col;
  for (size_t i = 0; i < 5; i++) {
    InstancedMesh& m = *meshes[i];
    assert(m.count == 3 && m.geometry->position()->count() == corners[i] && !m.frustumCulled && m.castShadow == (i == 0));
    assert(i < 2 ? !m.geometry->index && !m.geometry->uv() && m.geometry->attributes.size() == 2 && !m.instanceColor : m.geometry->index && m.geometry->uv() && m.instanceColor);
    m.geometry->computeBoundingSphere();
    expectSphere(*m.geometry->boundingSphere, spheres[i][0], spheres[i][1], spheres[i][2], spheres[i][3]);
    m.getMatrixAt(1, m4);
    const double* want = i == 1 || i == 4 ? box : head;
    for (size_t k = 0; k < 16; k++) assert(near(m4.elements[k], want[k], 2e-6 * (1 + std::fabs(want[k]))));
    // dark until the signals say what they show
    for (int k = 0; i >= 2 && k < 3; k++) {
      m.getColorAt(k, col);
      assert(near(col.r, 0.13, 1e-7) && near(col.g, 0.13, 1e-7) && near(col.b, 0.13, 1e-7));
    }
  }
  assert(meshes[2]->material == meshes[3]->material && meshes[2]->material != meshes[4]->material && meshes[2]->material->map && meshes[4]->material->map);
  assert(meshes[2]->material->map->anisotropy == 4 && meshes[2]->material->map->srgb && meshes[4]->material->map->anisotropy == 1 && meshes[4]->material->map->srgb);
  assert(meshes[2]->material->polygonOffset && meshes[2]->material->polygonOffsetFactor == -2 && meshes[2]->material->polygonOffsetUnits == -4 && meshes[2]->material->fog);
  assert(meshes[0]->material->color.getHex() == 0x1d2022 && meshes[1]->material->color.getHex() == 0xe8b800);
  // the figures: the left half of the picture the standing one, the right half the walking one; quads that face −z
  expectNumbers(*meshes[2]->geometry->uv(), 0, {0, 1, 0.5, 1, 0, 0, 0.5, 0}, "red uv");
  expectNumbers(*meshes[3]->geometry->uv(), 0, {0.5, 1, 1, 1, 0.5, 0, 1, 0}, "green uv");
  expectNumbers(*meshes[4]->geometry->uv(), 0, {0, 1, 1, 1, 0, 0, 1, 0}, "lamp uv");
  expectNumbers(*meshes[2]->geometry->position(), 0, {0.1, 2.5, -0.039, -0.1, 2.5, -0.039, 0.1, 2.3, -0.039, -0.1, 2.3, -0.039}, "red");
  expectNumbers(*meshes[2]->geometry->normal(), 0, {0, 0, -1, 0, 0, -1, 0, 0, -1, 0, 0, -1}, "red normal");
  expectNumbers(*meshes[2]->geometry->index, 0, {0, 2, 1, 2, 3, 1}, "red index");

  // the signals: no controller runs – the vehicles have green, so the people red; nobody has pressed a button
  const uint32_t red = meshes[2]->instanceColor->version, green = meshes[3]->instanceColor->version, lamp = meshes[4]->instanceColor->version;
  ped.update(signals);
  for (int k = 0; k < 3; k++) {
    meshes[2]->getColorAt(k, col);
    assert(near(col.r, 1, 1e-7) && near(col.g, 0.16, 1e-7) && near(col.b, 0.1, 1e-7));
    meshes[3]->getColorAt(k, col);
    assert(near(col.r, 0.13, 1e-7) && near(col.g, 0.13, 1e-7) && near(col.b, 0.13, 1e-7));
    meshes[4]->getColorAt(k, col);
    assert(near(col.r, 0.13, 1e-7) && near(col.g, 0.13, 1e-7) && near(col.b, 0.13, 1e-7));
  }
  assert(meshes[2]->instanceColor->version == red + 1 && meshes[3]->instanceColor->version == green + 1 && meshes[4]->instanceColor->version == lamp + 1);
  // (nothing changed: nothing goes to the graphics card)
  ped.update(signals);
  assert(meshes[2]->instanceColor->version == red + 1 && meshes[3]->instanceColor->version == green + 1 && meshes[4]->instanceColor->version == lamp + 1);

  // set up anew with fewer poles (the road network comes in pieces), and taken down
  ped.rebuild({poles[0]});
  assert(ped.group->children.size() == 5);
  for (const ObjectPtr& o : ped.group->children) assert(static_cast<InstancedMesh&>(*o).count == 1);
  ped.update(signals);
  ped.dispose();
  assert(ped.group->children.empty());
  ped.update(signals);
}

int main() {
  testCodes();
  testDrawing();
  testSizes();
  testAtlas();
  testAtlasFull();
  testPedLights();
  std::printf("render_signs_test: ok\n");
  return 0;
}
