// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/roadworks.ts @ v1.7.7
#include "render/roadworks.h"

#include <algorithm>
#include <cmath>
#include <set>

#include "audio/crowd.h"
#include "core/geom.h"
#include "engine/geometries.h"
#include "game/clock.h"
#include "platform/canvas.h"
#include "render/signs.h"
#include "sim/signals.h"

namespace fs {

namespace {

/** a site is built within this distance of the camera, and thrown away beyond the other (m) */
constexpr double BUILD = 600;
constexpr double DROP = 820;
/** the people of a march are drawn within this distance (m) */
constexpr double CROWD_NEAR = 420;
/** look numbers of the people at work: road workers in orange, police officers (render/people.ts gives the rest of them) */
constexpr int WORKER_LOOKS = 0x7100000;
constexpr int POLICE_LOOKS = 0x7200000;

constexpr int ORANGE = 0xf2661c;
constexpr int YELLOW = 0xf2c418;
constexpr int DARK = 0x26282b;

/** The red-and-white stripes of barriers, beacons and tape: one tile is half a metre square. */
TexturePtr stripeTexture() {
  const Canvas c(64, 64);
  const Ctx2D g = c.getContext2D();
  g.fillStyle("#f4f4f0");
  g.fillRect(0, 0, 64, 64);
  g.fillStyle("#d3192a");
  // two diagonal bands per tile, seamless when repeated
  for (const double o : {-64.0, 0.0, 64.0}) {
    g.beginPath();
    g.moveTo(o, 64);
    g.lineTo(o + 32, 64);
    g.lineTo(o + 96, 0);
    g.lineTo(o + 64, 0);
    g.closePath();
    g.fill();
  }
  TexturePtr t = canvasTexture(c);
  t->wrapS = Wrapping::Repeat;
  t->wrapT = Wrapping::Repeat;
  t->srgb = true;
  t->anisotropy = 4;
  return t;
}

GeometryPtr flat(const GeometryPtr& g) { return g->index ? g->toNonIndexed() : g; }

}  // namespace

class RoadworksRenderer::Parts {
 public:
  std::vector<GeometryPtr> plain;
  std::vector<GeometryPtr> striped;
  std::vector<GeometryPtr> lamps;
  std::vector<GeometryPtr> blinks;

  /** A piece painted in one colour (positions relative to the site), turned by yaw (and tipped by pitch before that). */
  void add(const GeometryPtr& g, int color, double x, double y, double z, double yaw = 0, double pitch = 0) {
    if (pitch) g->rotateX(pitch);
    if (yaw) g->rotateY(yaw);
    g->translate(x, y, z);
    GeometryPtr n = flat(g);
    const Color c(color);
    const size_t count = n->getAttribute("position")->count();
    std::vector<float> col(count * 3);
    for (size_t i = 0; i < count; i++) {
      col[i * 3] = static_cast<float>(c.r);
      col[i * 3 + 1] = static_cast<float>(c.g);
      col[i * 3 + 2] = static_cast<float>(c.b);
    }
    n->setAttribute("color", attr(std::move(col), 3));
    if (n->getAttribute("uv")) n->deleteAttribute("uv");
    plain.push_back(n);
  }

  void box(double w, double h, double d, int color, double x, double y, double z, double yaw = 0, double pitch = 0) { add(boxGeometry(w, h, d), color, x, y, z, yaw, pitch); }

  /** A board in the stripes: `w` × `h` × `d`, its stripes `w / 0.5` and `h / 0.5` tiles (mirrored with flip). */
  void stripes(double w, double h, double d, double x, double y, double z, double yaw, bool flip = false, double pitch = 0) {
    GeometryPtr g = boxGeometry(w, h, d);
    const AttrPtr uv = g->getAttribute("uv");
    for (size_t i = 0; i < uv->count(); i++) uv->setXY(i, (flip ? 1 - uv->getX(i) : uv->getX(i)) * std::max(0.5, w / 0.5), uv->getY(i) * std::max(0.5, h / 0.5));
    if (pitch) g->rotateX(pitch);
    g->rotateY(yaw);
    g->translate(x, y, z);
    striped.push_back(flat(g));
  }

  /** A warning lamp: steady (a barrier's) or flashing (a beacon's). */
  void lamp(double x, double y, double z, double yaw, bool flashing) {
    GeometryPtr g = cylinderGeometry(0.075, 0.075, 0.05, 10);
    g->rotateX(PI / 2).rotateY(yaw).translate(x, y, z);
    (flashing ? blinks : lamps).push_back(flat(g));
  }

  /** The merged meshes (none for an empty kind). */
  std::vector<std::shared_ptr<Mesh>> build(const MaterialPtr& plainMat, const MaterialPtr& stripeMat, const MaterialPtr& lampMat, const MaterialPtr& blinkMat) {
    std::vector<std::shared_ptr<Mesh>> out;
    const auto one = [&out](std::vector<GeometryPtr>& list, const MaterialPtr& mat, const char* name, bool shadow) {
      if (list.empty()) return;
      // (the attributes must be alike: without uv where any piece has none)
      bool uvs = true;
      for (const GeometryPtr& g : list) uvs = uvs && g->getAttribute("uv") != nullptr;
      if (!uvs)
        for (const GeometryPtr& g : list)
          if (g->getAttribute("uv")) g->deleteAttribute("uv");
      GeometryPtr g = mergeGeometries(list, false);
      list.clear();
      if (!g) return;
      if (!g->getAttribute("normal")) g->computeVertexNormals();
      g->computeBoundingSphere();
      auto m = make<Mesh>(g, mat);
      m->name = name;
      m->castShadow = shadow;
      m->receiveShadow = true;
      out.push_back(m);
    };
    one(plain, plainMat, "roadworks", true);
    one(striped, stripeMat, "roadworks: stripes", true);
    one(lamps, lampMat, "roadworks: lamps", false);
    one(blinks, blinkMat, "roadworks: flashing lamps", false);
    return out;
  }
};

RoadworksRenderer::RoadworksRenderer(AudioOut* audio)
    : people(std::make_unique<PeopleRenderer>(300)),
      stripeTex(stripeTexture()),
      plainMat(lambert({.vertexColors = true})),
      stripeMat(lambert({.map = stripeTex})),
      lampMat(lambert({.color = Color(0xc8a020), .emissive = Color(0x000000)})),
      blinkMat(lambert({.color = Color(0xc8a020), .emissive = Color(0x000000)})),
      offRed(lambert({.color = Color(0x3a0d0d)})),
      offAmber(lambert({.color = Color(0x3a2a08)})),
      offGreen(lambert({.color = Color(0x0b2a14)})),
      onRed(basic({.color = Color(0xff2a1a)})),
      onAmber(basic({.color = Color(0xffb21a)})),
      onGreen(basic({.color = Color(0x2aff6a)})),
      lampGeo(circleGeometry(0.085, 14)),
      noise(audio ? std::make_unique<CrowdNoise>(*audio) : nullptr) {
  group->name = "roadworks and demonstrations";
  group->add(people->group);
  GeometryPtr stick = boxGeometry(0.03, 1.35, 0.03);
  stick->translate(0, 0.675, 0);
  GeometryPtr board = boxGeometry(0.62, 0.46, 0.02);
  GeometryPtr mark = circleGeometry(0.13, 16);
  GeometryPtr banner = planeGeometry(1, 0.7);
  GeometryPtr band = planeGeometry(1, 0.16);
  const MaterialPtr cloth = lambert({.color = Color(0xffffff), .side = Side::Double});
  sticks = make<InstancedMesh>(stick, lambert({.color = Color(0x9a7a52)}), 160);
  boards = make<InstancedMesh>(board, lambert({.color = Color(0xffffff)}), 80);
  marks = make<InstancedMesh>(mark, lambert({.color = Color(0xffffff), .side = Side::Double}), 80);
  banners = make<InstancedMesh>(banner, cloth, 12);
  bands = make<InstancedMesh>(band, cloth, 12);
  for (const auto& m : {sticks, boards, marks, banners, bands}) {
    m->count = 0;
    m->frustumCulled = false;
    m->instanceMatrix->setDynamic();
    m->name = "demonstration: placards";
    group->add(m);
  }
}

RoadworksRenderer::~RoadworksRenderer() = default;

void RoadworksRenderer::update(RoadEvents* ev, SignRenderer* signs, double camX, double camZ, double time, double dt) {
  poses.clear();
  if (!ev) {
    people->update(poses, time);
    if (noise) noise->update(dt, JS_INF, 0, false);
    return;
  }
  // --- the sites near the camera: built once, dropped when far
  std::set<std::string> want;
  for (auto& [id, zp] : ev->zones) {
    WorkZone& z = *zp;
    if (std::hypot(z.x - camX, z.z - camZ) > BUILD) continue;
    const std::string key = "works:" + std::to_string(z.id) + ":" + std::to_string(z.seed);
    want.insert(key);
    if (!sites.count(key)) sites.emplace(key, buildWorks(z, ev->worksLayout(z), signs));
  }
  for (Demonstration& d : ev->demos) {
    if (!ev->marching(d) || std::hypot(d.x - camX, d.z - camZ) > BUILD + 900) continue;
    const std::string key = "demo:" + std::to_string(d.id);
    want.insert(key);
    const double length = d.route ? d.route->length : 0;
    auto it = sites.find(key);
    if (it != sites.end() && it->second.version != length) {
      drop(it->second);
      sites.erase(it);
    }
    if (!sites.count(key)) sites.emplace(key, buildClosure(ev->closureLayout(d), signs, length));
  }
  for (auto it = sites.begin(); it != sites.end();) {
    Site& site = it->second;
    if (!want.count(it->first) &&
        (startsWith(it->first, "demo:") || std::hypot(site.x - camX, site.z - camZ) > DROP || !zoneStill(*ev, it->first))) {
      drop(site);
      it = sites.erase(it);
    } else ++it;
  }
  // --- the lamps: lit from dusk to dawn, the beacons' flashing about once a second
  const double sun = sunAt(gameClock.ms()).elevation;
  const double night = std::min(1.0, std::max(0.0, (4 - sun) / 8));
  lampMat->emissive.setRGB(1, 0.72, 0.15).multiplyScalar(0.95 * night);
  const double flash = std::fmod(time, 1) < 0.45 ? 1 : 0;
  blinkMat->emissive.setRGB(1, 0.72, 0.15).multiplyScalar(1.1 * night * flash);
  // --- the temporary lights, the blue lights of the patrol cars
  for (auto& [key, site] : sites) {
    for (size_t i = 0; i < site.police.size(); i++) site.police[i]->setLights(true, time + i * 0.17);
    for (SiteLight& l : site.lights) {
      const int st = lightAt(*l.zone, l.group, ev->signals.time).state;
      l.red->material = st == SIG_RED || st == SIG_RED_AMBER ? onRed : offRed;
      l.amber->material = st == SIG_AMBER || st == SIG_RED_AMBER ? onAmber : offAmber;
      l.green->material = st == SIG_GREEN ? onGreen : offGreen;
    }
  }
  // --- the people at work
  for (auto& [id, zp] : ev->zones) {
    WorkZone& z = *zp;
    if (std::abs(z.x - camX) > 260 || std::abs(z.z - camZ) > 260) continue;
    const WorksLayout& L = ev->worksLayout(z);
    for (size_t i = 0; i < L.workers.size(); i++) worker(L.workers[i], time, static_cast<int>(i));
  }
  // --- the marches: their crowds, the officers at the barriers and beside the march
  int nSticks = 0;
  int nBoards = 0;
  int nBanners = 0;
  const double ms = ev->now();
  double heard = JS_INF;
  double heardSize = 0;
  bool heardWalking = false;
  for (Demonstration& d : ev->demos) {
    if (!ev->marching(d) || !d.route) continue;
    if (std::abs(d.x - camX) > 1500 || std::abs(d.z - camZ) > 1500) continue;
    // (how far its middle is: for what is heard of it)
    const MarchAt m = marchAt(d, ms);
    if (routeAt(ev->world, *d.route, m.head - (m.dir * crowdShape(d).length) / 2, 0, smp)) {
      const double dist = std::hypot(smp.x - camX, smp.z - camZ);
      if (dist < heard) {
        heard = dist;
        heardSize = d.size;
        heardWalking = m.walking;
      }
    }
    const ClosureLayout& L = ev->closureLayout(d);
    for (size_t i = 0; i < L.officers.size(); i++) {
      const auto& o = L.officers[i];
      if (std::abs(o.x - camX) < 300 && std::abs(o.z - camZ) < 300) {
        PersonPose p;
        p.x = o.x;
        p.y = o.y;
        p.z = o.z;
        p.yaw = o.yaw;
        p.walk = -1;
        p.look = police(d.id * 31 + static_cast<int>(i));
        poses.push_back(p);
      }
    }
    const std::array<int, 3> counts = crowd(*ev, d, ms, time, camX, camZ, nSticks, nBoards, nBanners);
    nSticks = counts[0];
    nBoards = counts[1];
    nBanners = counts[2];
  }
  finish(*sticks, nSticks);
  finish(*boards, nBoards);
  finish(*marks, nBoards);
  finish(*banners, nBanners);
  finish(*bands, nBanners);
  people->update(poses, time);
  if (noise) noise->update(dt, heard, heardSize, heardWalking);
}

bool RoadworksRenderer::zoneStill(RoadEvents& ev, const std::string& key) {
  const std::vector<std::string> parts = split(key, ":");
  const std::shared_ptr<WorkZone>* z = ev.zones.get(static_cast<int>(jsNumber(parts[1])));
  return z && (*z)->seed == jsNumber(parts[2]);
}

void RoadworksRenderer::drop(Site& site) {
  group->remove(site.group);
  // (the sign atlas may draw other signs where these were, once nothing else shows them)
  if (site.signs && site.signsBy) site.signsBy->release(*site.signs);
  for (const auto& m : site.police) {
    site.group->remove(m->group);
    policePool.push_back(m);
  }
  site.police.clear();
}

void RoadworksRenderer::finish(InstancedMesh& m, int n) {
  m.count = n;
  m.visible = n > 0;
  if (n > 0) {
    m.instanceMatrix->needsUpdate();
    if (m.instanceColor) m.instanceColor->needsUpdate();
  }
}

void RoadworksRenderer::worker(const WorksLayout::Worker& w, double time, int i) {
  const int look = WORKER_LOOKS + (w.look % 0xfffff);
  if (!worn.count(look)) {
    Look L = workerLook(look, "orange");
    Color trousers(ORANGE);
    trousers.multiplyScalar(0.9);
    L.legs = trousers;
    L.second = trousers;
    worn[look] = L;
  }
  people->wear(worn[look]);
  const double dx = w.x2 - w.x1;
  const double dz = w.z2 - w.z1;
  const double len = std::hypot(dx, dz);
  PersonPose p;
  p.look = look;
  p.y = w.y;
  if (len < 0.3) {
    p.x = w.x1;
    p.z = w.z1;
    p.yaw = w.yaw + 0.3 * std::sin(time * 0.2 + i);
    p.walk = -1;
    poses.push_back(p);
    return;
  }
  // (there, a pause, back, a pause: 0.8 m/s)
  const double leg = len / 0.8;
  const double cycle = 2 * leg + 8;
  const double t = std::fmod(time + i * 3.7, cycle);
  double f;
  bool walking = true;
  bool back = false;
  if (t < leg) f = t / leg;
  else if (t < leg + 4) {
    f = 1;
    walking = false;
  } else if (t < 2 * leg + 4) {
    f = 1 - (t - leg - 4) / leg;
    back = true;
  } else {
    f = 0;
    walking = false;
  }
  p.yaw = std::atan2(back ? dx : -dx, back ? dz : -dz);
  p.x = w.x1 + dx * f;
  p.z = w.z1 + dz * f;
  p.walk = walking ? (time + i * 3.7) * 0.8 : -1;
  poses.push_back(p);
}

int RoadworksRenderer::police(int n) {
  const int look = POLICE_LOOKS + (std::abs(n) % 0xfffff);
  if (!worn.count(look)) {
    Look L = workerLook(look, "yellow");
    const Color navy(0x1f2b45);
    if (L.seed < 0.45) L.top = navy;
    L.legs = navy;
    L.second = navy;
    if (L.seed < 0.5) L.hairColour = Color(0x1a2338);
    worn[look] = L;
  }
  people->wear(worn[look]);
  return look;
}

std::array<int, 3> RoadworksRenderer::crowd(RoadEvents& ev, const Demonstration& d, double ms, double time, double camX, double camZ, int sticks0, int boards0,
                                            int banners0) {
  int nSticks = sticks0;
  int nBoards = boards0;
  int nBanners = banners0;
  const DemoRoute& route = *d.route;
  const CrowdShape shape = crowdShape(d);
  const MarchAt ma = marchAt(d, ms);
  const double head = ma.head;
  const int dir = ma.dir;
  const bool walking = ma.walking;
  PolySample& p = smp;
  // (near enough to be seen at all?)
  if (!routeAt(ev.world, route, head - (dir * shape.length) / 2, 0, p) || std::hypot(p.x - camX, p.z - camZ) > CROWD_NEAR) return {nSticks, nBoards, nBanners};
  // (which way they face: the way the march goes – turned round when it walks back)
  const double back = dir < 0 ? PI : 0;
  const int n = std::min({d.size, crowdCap, 300 - static_cast<int>(poses.size()) - 8});
  const int cols = shape.cols;
  const auto r = [&d](int i, int k) { return hash01(d.seed * 7.0 + i * 131.0 + k * 7919.0); };
  static const int palette[] = {0xe63946, 0xf4a261, 0x2a9d8f, 0x457b9d, 0xffd166, 0x06d6a0, 0xef476f, 0x8338ec, 0x3a86ff, 0xfb5607, 0xffbe0b, 0x8ac926};
  const int nPal = 12;
  struct Pt {
    double x = 0, y = 0, z = 0, tx = 0, tz = 0;
  };
  const auto posFor = [&](int row, int col, int i, Pt& out) {
    const double u = head - dir * (row * shape.gap + 0.5 + (r(i, 1) - 0.5) * 0.5 + (walking ? 0.12 * std::sin(time * 0.7 + i) : 0));
    double s = 0;
    const Edge* at = routeAt(ev.world, route, u, 0, p, &s);
    if (!at) return false;
    const double half = std::max(1.0, at->width / 2 - 0.45);
    const double across = (((col + 0.5) / cols) * 2 - 1) * half * 0.94 + (r(i, 2) - 0.5) * 0.45;
    routeAt(ev.world, route, u, across, p);
    out.x = p.x;
    out.z = p.z;
    out.y = ev.world.edgeY(*at, s, p.x, p.z);
    out.tx = p.tx;
    out.tz = p.tz;
    return true;
  };
  Pt pt;
  for (int i = 0; i < n; i++) {
    const int row = i / cols;
    const int col = i % cols;
    if (!posFor(row, col, i, pt)) continue;
    const double along = std::atan2(-pt.tx, -pt.tz) + back;
    // standing at a rally: turned this way and that, towards the front
    const double yaw = along + (walking ? (r(i, 3) - 0.5) * 0.25 : (r(i, 3) - 0.5) * 1.1 + 0.25 * std::sin(time * 0.13 + i));
    PersonPose pose;
    pose.x = pt.x;
    pose.y = pt.y;
    pose.z = pt.z;
    pose.yaw = yaw;
    pose.walk = walking ? head * dir : -1;
    pose.look = static_cast<int>(std::floor(r(i, 4) * 99991));
    poses.push_back(pose);
    // a placard on a stick, held up in the right hand (a few rows from the front)
    if (r(i, 5) < 0.24 && nSticks < 150 && nBoards < 80 && row > 0) {
      const double fx = -std::sin(yaw);
      const double fz = -std::cos(yaw);
      const double hx = pt.x + std::cos(yaw) * 0.26 + fx * 0.08;
      const double hz = pt.z - std::sin(yaw) * 0.26 + fz * 0.08;
      const double sway = 0.12 * std::sin(time * (walking ? 2.4 : 0.8) + i);
      q.setFromAxisAngle(up, yaw + sway);
      m4.compose(v.set(hx, pt.y + 0.95, hz), q, sc.set(1, 1, 1));
      sticks->setMatrixAt(nSticks++, m4);
      m4.compose(v.set(hx, pt.y + 2.15, hz), q, sc.set(1, 1, 1));
      boards->setMatrixAt(nBoards, m4);
      boards->setColorAt(nBoards, col.setHex(palette[static_cast<int>(std::floor(r(i, 6) * nPal))]));
      // a round mark on its face (towards those who see the march come)
      const double mx = hx + fx * 0.012;
      const double mz = hz + fz * 0.012;
      m4.compose(v.set(mx, pt.y + 2.15 + (r(i, 7) - 0.5) * 0.12, mz), q, sc.setScalar(0.7 + r(i, 8) * 0.6));
      marks->setMatrixAt(nBoards, m4);
      marks->setColorAt(nBoards, col.setHex(r(i, 9) < 0.5 ? 0xffffff : palette[static_cast<int>(std::floor(r(i, 10) * nPal))]));
      nBoards++;
    }
  }
  // banners across the front rows: held between the outer two of a row
  for (const int row : {0, 4, 9}) {
    if (nBanners >= 12 || row * cols + cols > n) continue;
    Pt a;
    Pt b;
    if (!posFor(row, 0, row * cols, a) || !posFor(row, cols - 1, row * cols + cols - 1, b)) continue;
    const double w = std::hypot(b.x - a.x, b.z - a.z);
    if (w < 1.5) continue;
    const double yaw = std::atan2(-a.tx, -a.tz) + back;
    q.setFromAxisAngle(up, yaw);
    const double cx = (a.x + b.x) / 2 - std::sin(yaw) * 0.35;
    const double cz = (a.z + b.z) / 2 - std::cos(yaw) * 0.35;
    const double cy = (a.y + b.y) / 2;
    m4.compose(v.set(cx, cy + 1.5, cz), q, sc.set(w, 1, 1));
    banners->setMatrixAt(nBanners, m4);
    banners->setColorAt(nBanners, col.setHex(palette[static_cast<int>(std::floor(r(row, 11) * nPal))]));
    m4.compose(v.set(cx - std::sin(yaw) * 0.01, cy + 1.5, cz - std::cos(yaw) * 0.01), q, sc.set(w, 1, 1));
    bands->setMatrixAt(nBanners, m4);
    bands->setColorAt(nBanners, col.setHex(r(row, 12) < 0.5 ? 0xffffff : palette[static_cast<int>(std::floor(r(row, 13) * nPal))]));
    nBanners++;
    // its two poles
    for (const Pt* e : {&a, &b}) {
      if (nSticks >= 160) break;
      m4.compose(v.set(e->x - std::sin(yaw) * 0.35, e->y + 0.6, e->z - std::cos(yaw) * 0.35), q, sc.set(1, 1.05, 1));
      sticks->setMatrixAt(nSticks++, m4);
    }
  }
  // officers ahead of the march and behind it, at its sides
  const std::array<std::pair<double, int>, 4> ends = {{{head + dir * 5, 1}, {head + dir * 5, -1}, {head - dir * (shape.length + 4), 1}, {head - dir * (shape.length + 4), -1}}};
  for (int k = 0; k < 4; k++) {
    const auto [u, side] = ends[k];
    double s = 0;
    const Edge* at = routeAt(ev.world, route, u, 0, p, &s);
    if (!at) continue;
    const double off = side * std::max(0.8, at->width / 2 - 0.6);
    routeAt(ev.world, route, u, off, p);
    PersonPose pose;
    pose.x = p.x;
    pose.y = ev.world.edgeY(*at, s, p.x, p.z);
    pose.z = p.z;
    pose.yaw = std::atan2(-p.tx, -p.tz) + back;
    pose.walk = walking ? head * dir : -1;
    pose.look = police(d.id * 17 + 100 + k);
    poses.push_back(pose);
  }
  return {nSticks, nBoards, nBanners};
}

// ---------------------------------------------------------------------------- building

void RoadworksRenderer::signsOf(const std::vector<SignPost>& posts, Parts& parts, double ox, double oy, double oz, SignRenderer* signs, Site& site) {
  if (posts.empty()) return;
  for (const SignPost& s : posts) {
    // (the foot: a slab of recycled rubber)
    parts.box(0.42, 0.07, 0.75, 0x1d1d1f, s.x - ox, s.y - oy + 0.035, s.z - oz, s.yaw);
  }
  if (!signs) return;
  // (the sign renderer places them in the world's frame)
  std::shared_ptr<Group> g = signs->build(posts, ox, oy, oz);
  site.group->add(g);
  site.signsBy = signs;
  site.signs = g;
}

void RoadworksRenderer::barrier(const WorksBarrier& b, Parts& parts, double ox, double oy, double oz) {
  const double len = std::hypot(b.x2 - b.x1, b.z2 - b.z1);
  const double mx = (b.x1 + b.x2) / 2 - ox;
  const double mz = (b.z1 + b.z2) / 2 - oz;
  const double y = b.y - oy;
  // (the boards are 1.6 m wide at most: several side by side over a whole street)
  const int n = static_cast<int>(std::max(1.0, std::ceil(len / 1.7)));
  const double ux = (b.x2 - b.x1) / len;
  const double uz = (b.z2 - b.z1) / len;
  for (int i = 0; i < n; i++) {
    const double c = (i + 0.5) / n - 0.5;
    const double x = mx + ux * len * c;
    const double z = mz + uz * len * c;
    const double w = len / n - 0.08;
    parts.stripes(w, 0.25, 0.04, x, y + 1.0, z, b.yaw);
    for (const int e : {-1, 1}) {
      const double fx = x + ux * e * (w / 2 - 0.12);
      const double fz = z + uz * e * (w / 2 - 0.12);
      parts.box(0.04, 1.0, 0.04, 0xe8e8e4, fx, y + 0.5, fz, b.yaw);
      parts.box(0.36, 0.06, 0.62, 0x1d1d1f, fx, y + 0.03, fz, b.yaw);
    }
  }
  for (int k = 0; k < b.lamps; k++) {
    const double c = (k + 0.5) / b.lamps - 0.5;
    const double x = mx + ux * len * c * 0.9;
    const double z = mz + uz * len * c * 0.9;
    parts.box(0.18, 0.2, 0.08, 0x2a2a2c, x, y + 1.22, z, b.yaw);
    parts.lamp(x - std::sin(b.yaw) * 0.05, y + 1.24, z - std::cos(b.yaw) * 0.05, b.yaw, false);
  }
}

RoadworksRenderer::Site RoadworksRenderer::buildWorks(const WorkZone& z, const WorksLayout& L, SignRenderer* signs) {
  const double ox = z.x;
  const double oz = z.z;
  const double oy = L.pit ? L.pit->y : !L.barriers.empty() ? L.barriers[0].y : 0;
  Site site;
  site.x = z.x;
  site.z = z.z;
  site.group->name = "roadworks " + z.street;
  Parts parts;
  signsOf(L.signs, parts, ox, oy, oz, signs, site);
  for (const WorksBarrier& b : L.barriers) barrier(b, parts, ox, oy, oz);
  // beacons (Zeichen 605): a striped board on a foot, a lamp on top
  for (const WorksLayout::Beacon& b : L.beacons) {
    const double x = b.x - ox;
    const double y = b.y - oy;
    const double zz = b.z - oz;
    parts.stripes(0.25, 1.0, 0.035, x, y + 0.72, zz, b.yaw, b.pass < 0);
    parts.box(0.36, 0.09, 0.36, 0x1d1d1f, x, y + 0.045, zz, b.yaw);
    parts.box(0.03, 0.24, 0.03, 0x2a2a2c, x, y + 0.11, zz, b.yaw);
    if (b.lamp) parts.lamp(x - std::sin(b.yaw) * 0.04, y + 1.32, zz - std::cos(b.yaw) * 0.04, b.yaw, true);
  }
  // cones
  for (const WorksLayout::Cone& c : L.cones) {
    const double x = c.x - ox;
    const double y = c.y - oy;
    const double zz = c.z - oz;
    parts.box(0.38, 0.03, 0.38, 0x1d1d1f, x, y + 0.015, zz);
    parts.add(cylinderGeometry(0.1, 0.16, 0.2, 10, 1, true), ORANGE, x, y + 0.13, zz);
    parts.add(cylinderGeometry(0.083, 0.1, 0.07, 10, 1, true), 0xf4f4f0, x, y + 0.265, zz);
    parts.add(cylinderGeometry(0.06, 0.083, 0.1, 10, 1, true), ORANGE, x, y + 0.35, zz);
    parts.add(cylinderGeometry(0.045, 0.06, 0.06, 10, 1, true), 0xf4f4f0, x, y + 0.43, zz);
    parts.add(coneGeometry(0.045, 0.1, 10), ORANGE, x, y + 0.51, zz);
  }
  // the pit, the fence round it
  if (L.pit) {
    const WorksLayout::Pit& p = *L.pit;
    parts.box(p.wid, 0.03, p.len, 0x3b2f24, p.x - ox, p.y - oy + 0.015, p.z - oz, p.yaw);
    parts.box(p.wid * 0.7, 0.032, p.len * 0.8, 0x17120e, p.x - ox, p.y - oy + 0.017, p.z - oz, p.yaw);
  }
  const std::vector<double>& f = L.fence;
  const int nf = static_cast<int>(f.size() / 2);
  for (int i = 0; i < nf; i++) {
    const int j = (i + 1) % nf;
    const double x1 = f[i * 2] - ox;
    const double z1 = f[i * 2 + 1] - oz;
    const double x2 = f[j * 2] - ox;
    const double z2 = f[j * 2 + 1] - oz;
    const double len = std::hypot(x2 - x1, z2 - z1);
    if (len < 0.2) continue;
    const double yaw = std::atan2(x2 - x1, z2 - z1) + PI / 2;
    const int n = static_cast<int>(std::max(1.0, std::ceil(len / 2.1)));
    for (int k = 0; k < n; k++) {
      const double c = (k + 0.5) / n;
      const double x = x1 + (x2 - x1) * c;
      const double zc = z1 + (z2 - z1) * c;
      const double w = len / n - 0.06;
      const double y = L.fenceY - oy;
      // (an Absperrgitter: two striped rails on a frame, feet at the ends)
      parts.stripes(w, 0.1, 0.03, x, y + 0.95, zc, yaw);
      parts.stripes(w, 0.1, 0.03, x, y + 0.45, zc, yaw);
      for (const int e : {-1, 1}) {
        const double fx = x + ((x2 - x1) / len) * e * (w / 2);
        const double fz = zc + ((z2 - z1) / len) * e * (w / 2);
        parts.box(0.035, 1.0, 0.035, 0xd8d8d4, fx, y + 0.5, fz);
        parts.box(0.12, 0.05, 0.5, 0x2a2a2c, fx, y + 0.025, fz, yaw);
      }
    }
  }
  // the machines
  for (const WorksLayout::Machine& m : L.machines) {
    const double x = m.x - ox;
    const double y = m.y - oy;
    const double zz = m.z - oz;
    if (m.kind == "excavator") excavator(parts, x, y, zz, m.yaw);
    else if (m.kind == "heap") {
      GeometryPtr h1 = sphereGeometry(1, 12, 6, 0, PI * 2, 0, PI / 2);
      h1->scale(1.25, 0.7, 1.05);
      parts.add(h1, 0x8a8172, x, y, zz, m.yaw);
      GeometryPtr h2 = sphereGeometry(1, 8, 4, 0, PI * 2, 0, PI / 2);
      h2->scale(0.6, 0.35, 0.5);
      parts.add(h2, 0x9b917f, x + 0.6 * std::cos(m.yaw), y + 0.35, zz - 0.6 * std::sin(m.yaw), m.yaw);
    } else trailer(parts, x, y, zz, m.yaw);
  }
  // the temporary lights
  for (const WorksLayout::Light& l : L.lights) {
    const double x = l.x - ox;
    const double y = l.y - oy;
    const double zz = l.z - oz;
    parts.box(0.7, 0.12, 0.7, 0x1d1d1f, x, y + 0.06, zz, l.yaw);
    parts.box(0.08, 2.2, 0.08, 0x8a8f94, x, y + 1.1, zz, l.yaw);
    parts.box(0.38, 1.0, 0.24, YELLOW, x, y + 2.55, zz, l.yaw);
    parts.box(0.46, 1.08, 0.02, DARK, x + std::sin(l.yaw) * 0.12, y + 2.55, zz + std::cos(l.yaw) * 0.12, l.yaw);
    std::shared_ptr<Mesh> lamps[3];
    const MaterialPtr mats[3] = {offRed, offAmber, offGreen};
    for (int k = 0; k < 3; k++) {
      // (the faces of the lamps towards the traffic: −z of the light's frame)
      auto lamp = make<Mesh>(lampGeo, mats[k]);
      const double fy = y + 2.85 - k * 0.3;
      lamp->position.set(x - std::sin(l.yaw) * 0.125, fy, zz - std::cos(l.yaw) * 0.125);
      lamp->rotation.set(0, l.yaw + PI, 0);
      site.local->add(lamp);
      lamps[k] = lamp;
      parts.box(0.22, 0.04, 0.12, DARK, x - std::sin(l.yaw) * 0.17, fy + 0.11, zz - std::cos(l.yaw) * 0.17, l.yaw);
    }
    site.lights.push_back(SiteLight{&z, l.group, lamps[0], lamps[1], lamps[2]});
  }
  finishSite(site, parts, ox, oy, oz);
  return site;
}

void RoadworksRenderer::finishSite(Site& site, Parts& parts, double ox, double oy, double oz) {
  for (const auto& m : parts.build(plainMat, stripeMat, lampMat, blinkMat)) site.local->add(m);
  site.local->position.set(ox, oy, oz);
  site.group->add(site.local);
  group->add(site.group);
}

void RoadworksRenderer::excavator(Parts& parts, double x, double y, double z, double yaw) {
  const double s = std::sin(yaw);
  const double c = std::cos(yaw);
  // (a point of the excavator's frame: right a, up b, back d)
  const auto at = [&](double a, double d) { return std::pair<double, double>{x + c * a + s * d, z - s * a + c * d}; };
  for (const int side : {-1, 1}) {
    const auto [tx, tz] = at(side * 0.62, 0);
    parts.box(0.32, 0.38, 2.0, 0x1f2022, tx, y + 0.19, tz, yaw);
  }
  auto [px, pz] = at(0, 0);
  parts.box(0.9, 0.22, 1.5, 0x3a3c40, px, y + 0.3, pz, yaw);
  // the upper part: the body, the counterweight behind, the cab on the left
  parts.box(1.42, 0.62, 1.5, YELLOW, px, y + 0.72, pz, yaw);
  std::tie(px, pz) = at(0, 0.82);
  parts.box(1.42, 0.5, 0.36, 0xd9a910, px, y + 0.68, pz, yaw);
  std::tie(px, pz) = at(-0.22, -0.05);
  parts.box(0.86, 1.02, 0.95, 0x2b3a44, px, y + 1.54, pz, yaw);
  parts.box(0.92, 0.07, 1.0, YELLOW, px, y + 2.08, pz, yaw);
  for (const auto& [a, d] : {std::pair{-0.64, -0.5}, std::pair{0.2, -0.5}, std::pair{-0.64, 0.42}, std::pair{0.2, 0.42}}) {
    std::tie(px, pz) = at(a, d);
    parts.box(0.05, 1.05, 0.05, YELLOW, px, y + 1.55, pz, yaw);
  }
  // the boom up and forward, the arm down into the pit, the bucket
  std::tie(px, pz) = at(0.3, -1.25);
  parts.box(0.2, 0.22, 1.7, YELLOW, px, y + 1.45, pz, yaw, 0.62);
  std::tie(px, pz) = at(0.3, -2.2);
  parts.box(0.16, 0.16, 1.4, 0xd9a910, px, y + 1.25, pz, yaw, -1.05);
  std::tie(px, pz) = at(0.3, -2.55);
  parts.box(0.5, 0.36, 0.34, 0x2a2a2c, px, y + 0.42, pz, yaw, 0.4);
  // the dozer blade at the front
  std::tie(px, pz) = at(0, -1.12);
  parts.box(1.5, 0.3, 0.08, YELLOW, px, y + 0.2, pz, yaw);
}

void RoadworksRenderer::trailer(Parts& parts, double x, double y, double z, double yaw) {
  const double s = std::sin(yaw);
  const double c = std::cos(yaw);
  const auto at = [&](double a, double d) { return std::pair<double, double>{x + c * a + s * d, z - s * a + c * d}; };
  parts.box(1.9, 1.95, 3.4, 0xd5d8d2, x, y + 1.55, z, yaw);
  parts.box(1.96, 0.08, 3.46, 0x8e9396, x, y + 2.56, z, yaw);
  auto [px, pz] = at(0.96, 0.6);
  parts.box(0.04, 1.55, 0.75, 0x6d7478, px, y + 1.35, pz, yaw);
  std::tie(px, pz) = at(0.96, -0.9);
  parts.box(0.04, 0.55, 0.8, 0x33414a, px, y + 1.8, pz, yaw);
  for (const int side : {-1, 1}) {
    std::tie(px, pz) = at(side * 0.98, 0.3);
    GeometryPtr wheel = cylinderGeometry(0.3, 0.3, 0.2, 12);
    wheel->rotateZ(PI / 2);
    parts.add(wheel, 0x1d1d1f, px, y + 0.3, pz, yaw);
  }
  std::tie(px, pz) = at(0, -2.3);
  parts.box(0.08, 0.08, 1.4, 0x55595c, px, y + 0.45, pz, yaw);
  std::tie(px, pz) = at(0, -2.95);
  parts.box(0.06, 0.45, 0.06, 0x55595c, px, y + 0.24, pz, yaw);
}

RoadworksRenderer::Site RoadworksRenderer::buildClosure(const ClosureLayout& L, SignRenderer* signs, double version) {
  const WorksBarrier* first = L.barriers.empty() ? nullptr : &L.barriers[0];
  const double ox = first ? (first->x1 + first->x2) / 2 : 0;
  const double oz = first ? (first->z1 + first->z2) / 2 : 0;
  const double oy = first ? first->y : 0;
  Site site;
  site.x = ox;
  site.z = oz;
  site.version = version;
  site.group->name = "closure of a march";
  Parts parts;
  signsOf(L.signs, parts, ox, oy, oz, signs, site);
  for (const WorksBarrier& b : L.barriers) barrier(b, parts, ox, oy, oz);
  // the tape: a band at hip height, sagging a little in the middle
  for (const ClosureLayout::Tape& t : L.tape) {
    const double len = std::hypot(t.x2 - t.x1, t.z2 - t.z1);
    if (len < 0.3) continue;
    const double yaw = std::atan2(t.x2 - t.x1, t.z2 - t.z1) + PI / 2;
    const int n = static_cast<int>(std::max(1.0, std::ceil(len / 3)));
    for (int k = 0; k < n; k++) {
      const double c = (k + 0.5) / n;
      const double sag = 0.12 * std::sin(PI * c);
      parts.stripes(len / n, 0.07, 0.004, t.x1 + (t.x2 - t.x1) * c - ox, t.y - oy + 1.05 - sag, t.z1 + (t.z2 - t.z1) * c - oz, yaw);
    }
  }
  // a patrol car behind the barriers at the ends of the march: blue lights on, its "STOP POLIZEI" sign raised
  for (const ClosureLayout::Stand& c : L.cars) {
    std::shared_ptr<EmergencyModel> m;
    if (!policePool.empty()) {
      m = policePool.back();
      policePool.pop_back();
    } else if (buildPoliceCar) m = buildPoliceCar();
    if (!m) continue;
    m->group->position.set(c.x, c.y, c.z);
    m->group->rotation.set(0, c.yaw, 0);
    m->setSign(true);
    m->setMotion(0, false, false, false);
    site.group->add(m->group);
    site.police.push_back(m);
  }
  finishSite(site, parts, ox, oy, oz);
  return site;
}

}  // namespace fs
