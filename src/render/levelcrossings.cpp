// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/levelcrossings.ts @ v1.7.7
#include "render/levelcrossings.h"

#include <cmath>
#include <set>

#include "audio/crossingbell.h"
#include "core/geom.h"
#include "engine/geometries.h"
#include "platform/canvas.h"
#include "render/signs.h"
#include "sim/levelcrossing.h"

namespace fs {

namespace {

/** installations are drawn within this distance of the camera (m) */
constexpr double REACH = 700;

/** red and white stripes, half a metre each: the boom of a barrier */
TexturePtr stripeTexture() {
  const Canvas c(64, 8);
  const Ctx2D g = c.getContext2D();
  g.fillStyle("#f2f2ee");
  g.fillRect(0, 0, 64, 8);
  g.fillStyle("#c4161c");
  g.fillRect(0, 0, 32, 8);
  TexturePtr t = canvasTexture(c);
  t->wrapS = Wrapping::Repeat;
  t->srgb = true;
  return t;
}

PolySample smp;

}  // namespace

LevelCrossingRenderer::LevelCrossingRenderer(AudioOut* audio)
    : bell(audio ? std::make_unique<CrossingBell>(*audio) : nullptr),
      mastMat(lambert({.color = Color(0x9aa0a4)})),
      housingMat(lambert({.color = Color(0x17181a)})),
      plateMat(lambert({.color = Color(0xf1f1ec)})),
      driveMat(lambert({.color = Color(0xd9b21c)})),
      boomMat(lambert({.map = stripeTexture()})),
      offRed(lambert({.color = Color(0x3b0c0c)})),
      offYellow(lambert({.color = Color(0x3d300a)})),
      onRed(basic({.color = Color(0xff2414)})),
      onYellow(basic({.color = Color(0xffbb1c)})),
      lampGeo(circleGeometry(0.105, 16)),
      smallLampGeo(sphereGeometry(0.05, 8, 6)),
      mastGeo(cylinderGeometry(0.055, 0.055, 1, 8)) {
  mastGeo->translate(0, 0.5, 0);
  group->name = "level crossings";
}

LevelCrossingRenderer::~LevelCrossingRenderer() = default;

void LevelCrossingRenderer::update(LevelCrossings& lx, SignRenderer* signs, double camX, double camZ, double time) {
  std::set<int> near;
  double bellDist = JS_INF;
  for (const auto& cp : lx.list) {
    const LevelCrossing& c = *cp;
    const double d = std::hypot(c.x - camX, c.z - camZ);
    if (d > REACH) continue;
    if (passive(c) && !c.beacons) continue;
    near.insert(c.id);
    auto it = sites.find(c.id);
    if (it == sites.end() || it->second.c != &c) {
      if (it != sites.end()) drop(it->second);
      it = sites.insert_or_assign(c.id, build(lx, c, signs)).first;
    }
    pose(it->second, time);
    if (ringing(c)) bellDist = std::min(bellDist, d);
  }
  for (auto it = sites.begin(); it != sites.end();) {
    if (near.count(it->first)) {
      ++it;
      continue;
    }
    drop(it->second);
    it = sites.erase(it);
  }
  if (bell) bell->update(bellDist);
}

void LevelCrossingRenderer::drop(Site& site) {
  group->remove(site.group);
  for (Side& s : site.sides) s = Side{};
  if (site.signs && site.signsBy) site.signsBy->release(*site.signs);
}

void LevelCrossingRenderer::pose(Site& site, double time) {
  const LevelCrossing& c = *site.c;
  const bool red = redOn(c) && (c.eq.light != "blink" || std::fmod(time, 1) < 0.55);
  const bool yellow = c.phase == "yellow";
  // (the lamps on the booms burn while they are not up)
  const bool boomLit = c.lower > 0.02 && std::fmod(time, 1) < 0.6;
  for (Side& s : site.sides) {
    if (s.yellow) s.yellow->material = yellow ? onYellow : offYellow;
    for (const auto& m : s.red) m->material = red ? onRed : offRed;
    if (s.arm) s.arm->rotation.z = (1 - c.lower) * 1.48;
    for (const auto& m : s.boomLamps) m->material = boomLit ? onRed : offRed;
  }
}

LevelCrossingRenderer::Site LevelCrossingRenderer::build(LevelCrossings& lx, const LevelCrossing& c, SignRenderer* signs) {
  const Edge& e = *c.edge;
  const World& world = lx.world;
  auto g = make<Group>();
  const double y0 = world.edgeY(e, (c.s0 + c.s1) / 2, c.x, c.z);
  g->position.set(c.x, y0, c.z);
  group->add(g);
  Site site;
  site.c = &c;
  site.group = g;
  const auto groundY = [&world, &e](double s, double x, double z) { return world.edgeY(e, s, x, z); };
  if (!passive(c)) {
    for (const int dir : {1, -1}) {
      if ((dir > 0 ? e.lanesF : e.lanesB) == 0) continue;
      site.sides.push_back(side(lx, c, dir, *g, y0));
    }
  }
  // the cross on the masts, the beacons
  const std::vector<SignPost> posts = lx.signsFor(c, groundY);
  if (signs && !posts.empty()) {
    std::shared_ptr<Group> built = signs->build(posts, c.x, y0, c.z);
    g->add(built);
    // (the sign renderer places them in the world's frame)
    built->position.set(-c.x, -y0, -c.z);
    site.signsBy = signs;
    site.signs = built;
  }
  return site;
}

LevelCrossingRenderer::Side LevelCrossingRenderer::side(LevelCrossings& lx, const LevelCrossing& c, int dir, Group& group, double y0) {
  const Edge& e = *c.edge;
  const double entry = entryOf(c, dir);
  const World& world = lx.world;
  const SignPost post = lx.postAt(e, entry, dir, {}, [&world, &e](double s, double x, double z) { return world.edgeY(e, s, x, z); });
  const double ox = post.x - c.x;
  const double oz = post.z - c.z;
  const double oy = post.y - y0;
  const double fx = -std::sin(post.yaw);
  const double fz = -std::cos(post.yaw);
  Side side;
  // the mast, up to the cross
  auto mast = make<Mesh>(mastGeo, mastMat);
  mast->position.set(ox, oy - 0.2, oz);
  mast->scale.set(1, 2.65, 1);
  group.add(mast);
  // the lights: a housing facing the traffic, its lamps on the front
  auto head = make<Group>();
  head->position.set(ox + fx * 0.09, oy, oz + fz * 0.09);
  head->rotation.y = post.yaw;
  group.add(head);
  const auto add = [&head](const GeometryPtr& geo, const MaterialPtr& mat, double x, double y, double z) {
    auto m = make<Mesh>(geo, mat);
    m->position.set(x, y, z);
    head->add(m);
    return m;
  };
  // (in the head's frame the traffic is at −z: a plane faces +z, so the lamps are turned round)
  if (c.eq.light == "yellowRed") {
    add(boxGeometry(0.36, 0.78, 0.16), housingMat, 0, 1.95, 0);
    auto yl = make<Mesh>(lampGeo, offYellow);
    yl->position.set(0, 2.15, -0.082);
    yl->rotation.y = PI;
    head->add(yl);
    auto rd = make<Mesh>(lampGeo, offRed);
    rd->position.set(0, 1.78, -0.082);
    rd->rotation.y = PI;
    head->add(rd);
    side.yellow = yl;
    side.red.push_back(rd);
  } else if (c.eq.light == "blink") {
    // (the round white plate with the red lamp of the older Blinklicht)
    GeometryPtr plate = cylinderGeometry(0.3, 0.3, 0.05, 20);
    plate->rotateX(PI / 2);
    add(plate, plateMat, 0, 1.95, 0);
    auto rd = make<Mesh>(lampGeo, offRed);
    rd->position.set(0, 1.95, -0.03);
    rd->rotation.y = PI;
    head->add(rd);
    side.red.push_back(rd);
  }
  // the barrier: its drive just past the cross, the boom across the lanes it closes
  if (c.eq.barrier != "none") {
    samplePolyline(e.pl, entry, smp);
    const double tx = smp.tx * dir;
    const double tz = smp.tz * dir;
    // (towards the middle of the road: to the left of the direction of travel)
    const double bx = tz;
    const double bz = -tx;
    const double outward = std::hypot(ox + c.x - smp.x, oz + c.z - smp.z);
    const double len = c.eq.barrier == "full" ? std::max(2.0, outward * 2 - 0.8) : std::max(1.5, outward - 0.15);
    const double px = ox + tx * 0.9;
    const double pz = oz + tz * 0.9;
    GeometryPtr driveGeo = boxGeometry(0.34, 1.05, 0.34);
    driveGeo->translate(0, 0.52, 0);
    auto drive = make<Mesh>(driveGeo, driveMat);
    drive->position.set(px - bx * 0.25, oy, pz - bz * 0.25);
    drive->rotation.y = std::atan2(-bz, bx);
    group.add(drive);
    auto pivot = make<Object3D>();
    pivot->position.set(px, oy + 0.95, pz);
    pivot->rotation.y = std::atan2(-bz, bx);
    group.add(pivot);
    auto arm = make<Object3D>();
    pivot->add(arm);
    GeometryPtr boomGeo = boxGeometry(len, 0.09, 0.07);
    boomGeo->translate(len / 2, 0, 0);
    // (the stripes: a metre of red and white, along the boom)
    const AttrPtr uv = boomGeo->getAttribute("uv");
    for (size_t i = 0; i < uv->count(); i++) uv->setX(i, uv->getX(i) * len);
    arm->add(make<Mesh>(boomGeo, boomMat));
    for (const double f : {0.35, 0.65, 0.95}) {
      auto lamp = make<Mesh>(smallLampGeo, offRed);
      lamp->position.set(len * f, 0.07, 0);
      arm->add(lamp);
      side.boomLamps.push_back(lamp);
    }
    side.arm = arm;
  }
  return side;
}

}  // namespace fs
